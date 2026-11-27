#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

#include "Engine/Logging/Log.h"

#include <cstdio>
#include <string>

namespace Engine
{

	using namespace FrameRendererDetail;

	static_assert(
		sizeof(R::GpuReflectionProbeRecord) == R::ScreenSpaceProbeRecordBytes && R::MaxReflectionProbes == R::ScreenSpaceMaxProbes);

	void FrameRenderer::BeginFrame()
	{
		// No GPU wait here: the previous frame keeps running on the GPU while the engine
		// updates UI, gameplay, physics and render extraction for the next one. Every
		// Collect/Update below only checks completion values (non-blocking); Render waits
		// for the previous submission right before it acquires and records (GatherTimings).
		impl->scene->Collect();
		impl->materialTable->Collect();
		impl->particles->Collect();
		impl->skinning->Collect();
		impl->atlasTextures->Collect();
		impl->viewports->Collect();
		impl->bindless->Collect(); // Also done by the UI owners; the scene's textures must not depend on them.
		impl->CollectPrograms();
		impl->meshes->FlushReleases(lastCompletion);
		impl->residency->Update();
		impl->materials->Update();
	}

	namespace
	{

		// Why the camera cannot render, or nullptr when it can: a view or projection with a
		// non-finite entry or that cannot be inverted (the same check BuildScreenSpaceParams
		// makes), or a non-finite position, basis, field of view, aspect or near plane. Far is
		// not checked: nothing reads it, and an infinite far plane is legitimate.
		const char* CameraProblem(const RenderCamera& camera)
		{
			if (const char* problem = R::ScreenSpace::ViewProjectionProblem(camera.View, camera.Projection))
			{
				return problem;
			}

			const auto finite = [](const auto& values)
			{
				return std::all_of(values.begin(), values.end(),
					[](float value)
					{
						return std::isfinite(value);
					});
			};

			if (!finite(camera.Position))
			{
				return "the position is not finite";
			}

			if (!finite(camera.Forward) || !finite(camera.Right) || !finite(camera.Up))
			{
				return "the forward, right or up direction is not finite";
			}

			const auto& f = camera.Forward;

			if (!(f[0] * f[0] + f[1] * f[1] + f[2] * f[2] > 1.0e-12f))
			{
				return "the forward direction has no length";
			}

			if (!std::isfinite(camera.VerticalFov) || !std::isfinite(camera.Aspect) || !std::isfinite(camera.Near))
			{
				return "the field of view, aspect or near plane is not finite";
			}

			return nullptr;
		}

		// The rejected camera's values for the warning (failure path only).
		std::string DescribeCamera(const RenderCamera& camera)
		{
			std::string text;
			const auto append = [&](const char* name, const float* values, std::size_t count)
			{
				text += ' ';
				text += name;
				text += " [";

				for (std::size_t i = 0; i < count; ++i)
				{
					char number[32];
					std::snprintf(number, sizeof(number), i ? " %.9g" : "%.9g", double(values[i]));
					text += number;
				}

				text += ']';
			};
			append("view", camera.View.data(), camera.View.size());
			append("projection", camera.Projection.data(), camera.Projection.size());
			append("position", camera.Position.data(), camera.Position.size());
			append("forward", camera.Forward.data(), camera.Forward.size());
			const std::array<float, 4> lens{ camera.VerticalFov, camera.Aspect, camera.Near, camera.Far };
			append("fov/aspect/near/far", lens.data(), lens.size());
			return text;
		}

		// The view's right and up, scaled by the half-angle tangents (the sky's ray basis).
		std::pair<std::array<float, 3>, std::array<float, 3>> SkyRays(const RenderCamera& camera)
		{
			const float tanY = std::tan(camera.VerticalFov * 0.5f);
			const float tanX = tanY * camera.Aspect;
			std::array<float, 3> right{};
			std::array<float, 3> up{};

			for (int c = 0; c < 3; ++c)
			{
				right[c] = camera.Right[c] * tanX;
				up[c] = camera.Up[c] * tanY;
			}

			return { right, up };
		}

		R::GraphTexture CreateBackDepth(R::RenderGraph& graph, std::uint32_t width, std::uint32_t height)
		{
			S::TextureDesc backDesc;
			backDesc.Extent = { width, height, 1 };
			backDesc.PixelFormat = R::CanonicalDepthFormat;
			backDesc.Usage = S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled;
			backDesc.DebugName = "Back-face depth";
			return graph.CreateTexture(backDesc);
		}

		void ResetSceneStats(RenderStats& stats, const RenderFrameInput& input, bool draw3D, std::size_t pageSlots)
		{
			stats.ShadowViews = 0;
			stats.ShadowViewsRendered = 0;
			stats.ShadowCasters = static_cast<std::uint32_t>(input.ShadowCasters.size());
			stats.Rendered3D = draw3D;
			stats.PageSlots = static_cast<std::uint32_t>(pageSlots);
			stats.ReflectionProbes = 0;
			stats.ReflectionProbeFaces = 0;
			stats.PlanarReflections = 0;
			stats.PlanarCaptures = 0;
			stats.PlanarCandidates = 0;
		}

		struct PageSlotLists
		{
			std::vector<R::ForwardPlusPageSlot> Forward;
			std::vector<R::ShadowPageSlot> Shadow;
			std::vector<std::uint32_t> Index;
		};

		PageSlotLists SplitPageSlots(const std::vector<MeshLibrary::PageSlot>& pageSlots)
		{
			PageSlotLists lists;

			for (const auto& slot : pageSlots)
			{
				lists.Forward.push_back({ slot.IndexPage, slot.VertexPage });
				lists.Shadow.push_back({ slot.IndexPage, slot.VertexPage });
				lists.Index.push_back(slot.IndexPage);
			}

			return lists;
		}

		R::CompiledRenderGraph CompileGraph(R::RenderGraph& graph, R::RenderGraphCompileCache& cache)
		{
			// SWIM_GRAPH_CACHE=off compiles every frame (diagnostics: compare the two).
			static const bool cacheGraphs = []
			{
				const char* mode = std::getenv("SWIM_GRAPH_CACHE");
				return mode == nullptr || std::string_view(mode) != "off";
			}();
			return cacheGraphs ? graph.Compile(cache) : graph.Compile();
		}

		void CopyExecuteTimings(RenderStats& stats, const R::RenderGraphExecutor::ExecuteTimings& t)
		{
			stats.RecordPhases = { { "Executor: wait", t.Wait }, { "Executor: reset and recycle tables", t.Reset },
				{ "Executor: allocate transients", t.Allocate }, { "Executor: stage uploads", t.Stage },
				{ "Executor: query pool", t.Queries }, { "Executor: record passes", t.Record },
				{ "Executor: barriers (in record)", t.Barriers }, { "Executor: submit", t.Submit } };
			stats.RecordPasses.clear();

			for (const auto& pass : t.Passes)
			{
				stats.RecordPasses.push_back({ pass.Name, pass.Nanoseconds.value_or(0.0) * 1.0e-6 });
			}
		}

		// Waits for the frame and copies its pixels into capture (cleared when not ready).
		bool ReadCapture(R::RenderGraphExecutor& executor, const R::GraphReadback& readback, std::uint32_t width, std::uint32_t height,
			std::vector<std::uint8_t>& capture)
		{
			executor.Wait();
			capture.resize(std::size_t(width) * height * 4);

			if (executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(capture))) == S::ReadbackStatus::Ready)
			{
				return true;
			}

			capture.clear();
			return false;
		}
	} // namespace

	std::uint32_t FrameRenderer::Impl::GlintIndex()
	{
		if (glintTexture)
		{
			return bindless->GetIndex(*glintTexture);
		}

		return glintAsset.IsValid() ? residency->GetBindlessIndex(glintAsset) : 0u;
	}

	void FrameRenderer::Impl::ResetHistories(bool cameraCut, bool temporalOn)
	{
		if (cameraCut)
		{
			reflectionHistoryValid = false;
			ssrColorValid = false;
			temporal->ResetHistory();
			post->ResetExposureHistory();
			previousViewProjection.reset();
		}

		if (!temporalOn)
		{
			temporal->ResetHistory();
		}
	}

	void FrameRenderer::Impl::RecordScene(const SceneInputs& in, FeatureGeometry& geometry, CpuLaps& lap,
		std::optional<R::ScreenSpaceFrame::ProbeInputs>& probeInputs, std::optional<R::ScreenSpaceFrame::PlanarInputs>& planarInputs)
	{
		lap("Build: Sky and scene setup");
		ScenePasses scenePasses;
		RecordShadows(in, geometry, scenePasses);

		// Shadows off: nothing above ran; the time below is the features' own geometry.
		lap("Build: Shadows");

		if (!geometry.Prepared)
		{
			PrepareFeatureGeometry(in, geometry);
			lap("Build: Feature geometry (PrepareGeometry)");
		}

		RecordVisibility(in, scenePasses);
		lap("Build: Visibility and clusters");
		RecordForward(in, geometry, scenePasses);
		lap("Build: Forward+");
		const CaptureInputs captures{ in.Graph, in.Settings, in.Input, in.Stats, in.IndexPages, in.Geometry, in.Scene, in.Lights,
			in.Environment, in.Targets, scenePasses.Grid, scenePasses.Forward };
		RecordReflectionProbes(captures, probeInputs);
		RecordPlanarReflections(captures, in.Width, in.Height, in.ViewProjection, probeInputs, planarInputs);
	}

	std::optional<R::ParticleGraphResources> FrameRenderer::Impl::SimulateParticles(const FrameBuildInputs& in, FramePending& pending)
	{
		in.Stats.ParticleEmitters = particles->GetStats().LiveEmitters;

		if (!in.Settings.Particles || in.Stats.ParticleEmitters == 0)
		{
			return std::nullopt;
		}

		const auto& camera = in.Input.Camera;
		R::ParticleView particleView;
		particleView.View = camera.View;
		particleView.Projection = camera.Projection;
		particleView.Jitter = in.Jitter;
		const float delta = in.Input.SimulationDeltaTime;
		const float dt = std::clamp(std::isfinite(delta) ? delta : 0.0f, 0.0f, 0.1f);
		auto simulated = particles->Simulate(in.Graph, particleView, dt);
		pending.Particles = true;
		return simulated;
	}

	S::TimelinePoint FrameRenderer::Impl::BuildAndExecute(const FrameBuildInputs& in, FrameBuildOutputs& out)
	{
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		auto& lap = in.Lap;

		// Which GeometryHeap pages this frame's draws come from.
		const auto& pageSlots = meshes->CollectPageSlots();
		const bool draw3D = !pageSlots.empty();
		const auto slots = SplitPageSlots(pageSlots);

		if (draw3D)
		{
			EnsureVisibility(static_cast<std::uint32_t>(pageSlots.size()));
		}

		// --- Uploads and persistent imports ---------------------------------------
		const auto imports = ImportFrame(graph, out.Pending);

		// The view render features see (the features themselves run further down; the
		// environment below also asks them for their contribution).
		const RenderFeatureView featureView =
			BuildFeatureView(in.Input, settings, in.ViewProjection, in.Jitter, in.Cut, in.Temporal, in.Width, in.Height);
		FeatureGeometry featureGeometry;

		lap("Build: Uploads and imports");
		std::optional<R::EnvironmentGraphResources> environment;
		std::optional<R::GraphTexture> lut;
		RecordEnvironment({ graph, settings, featureView, in.Features, draw3D },
			{ environment, lut, out.EnvironmentRebuilt, out.EnvironmentWithFeatures });

		lap("Build: Environment");
		// --- Frame targets ----------------------------------------------------------
		R::ForwardPlusTargets targets = CreateForwardTargets(graph, in.Width, in.Height, "");
		// Multisampled scene geometry (RenderSettings::MsaaSamples): the sky and the opaque passes
		// draw into these, resolved into targets before anything else reads them.
		std::optional<R::ForwardPlusTargets> msaaTargets;
		const auto msaaSamples = draw3D ? MsaaSampleCount(settings) : S::SampleCount::X1;

		if (msaaSamples != S::SampleCount::X1)
		{
			msaaTargets = CreateMsaaTargets(graph, in.Width, in.Height, msaaSamples);
		}

		if (draw3D && settings.ScreenSpace.Reflections.Enabled && settings.ScreenSpace.Reflections.BackFaces &&
			forward->SupportsBackDepth())
		{
			targets.BackDepth = CreateBackDepth(graph, in.Width, in.Height);
		}

		lap("Build: Frame targets");
		const auto& camera = in.Input.Camera;
		const auto [right, up] = SkyRays(camera);
		RecordSky(graph, settings, msaaTargets ? *msaaTargets : targets, right, up, camera.Forward, in.Width, in.Height);

		R::RenderViewDesc viewDesc;
		viewDesc.ViewProjection = in.ViewProjection;
		viewDesc.CameraPosition = camera.Position;
		viewDesc.Flags = (camera.Cut ? std::uint32_t(R::GpuViewFlags::CameraCut) : 0u) | std::uint32_t(R::GpuViewFlags::Overlay);
		ResetSceneStats(in.Stats, in.Input, draw3D, pageSlots.size());
		in.Stats.MsaaSamples = static_cast<std::uint32_t>(msaaSamples);
		in.Stats.SkyCubemap = skyCubeImport && skyCubeImportGraph == &graph && skyCubeImportFrame == frameIndex;
		std::optional<R::ScreenSpaceFrame::ProbeInputs> probeInputs;
		std::optional<R::ScreenSpaceFrame::PlanarInputs> planarInputs;

		const SceneInputs sceneInputs{ graph, settings, in.Input, in.Stats, in.Features, featureView, in.Width, in.Height,
			in.ViewProjection, in.Jitter, in.Glint, viewDesc, slots.Forward, slots.Shadow, slots.Index, imports.Residency.Geometry,
			imports.Materials, imports.Scene, imports.Lights, environment, lut, targets, draw3D, in.Temporal,
			msaaTargets ? &*msaaTargets : nullptr };

		if (draw3D)
		{
			RecordScene(sceneInputs, featureGeometry, lap, probeInputs, planarInputs);
		}

		lap("Build: Reflection probes");
		// --- Particles (simulation; drawn after the composite, below) ---------------
		const auto simulatedParticles = SimulateParticles(in, out.Pending);

		lap("Build: Particles");
		// --- Screen-space effects, TAA and post -------------------------------------
		const auto screen = RecordScreenSpace(
			{ graph, settings, camera, in.Width, in.Height, in.Jitter, in.Temporal, draw3D, targets, probeInputs, planarInputs });

		auto postOutput = RecordPost(sceneInputs, screen.Output, simulatedParticles);

		lap("Build: Screen space, TAA, features and post");
		postOutput = RecordUi(sceneInputs, postOutput, in.LastCompletion, out.Pending.Atlas, out.Pending.Viewports);

		lap("Build: UI");
		// --- Capture and presentation -------------------------------------------------
		if (in.Input.Capture)
		{
			out.Capture = R::AddTextureReadback(graph, "Frame capture", postOutput, { 0, {}, {}, { in.Width, in.Height, 1 } });
		}

		RecordPresentation(graph, in.Frame, in.LateAcquire, postOutput, in.Width, in.Height);

		lap("Build: capture and presentation");
		const auto compiled = CompileGraph(graph, graphCache);
		lap("Graph compile");
		auto& executor = in.Device.GetExecutor();
		const auto completion = executor.Execute(compiled, in.Device.GetSubmit(in.Frame));
		out.Submitted = completion;
		lap("Record and submit");
		CopyExecuteTimings(in.Stats, executor.GetLastExecuteTimings());
		return completion;
	}

	S::TimelinePoint FrameRenderer::Impl::AcquireAndCopy(RenderDevice& device, RenderDevice::Frame& frame, S::TimelinePoint completion,
		std::uint32_t width, std::uint32_t height, CpuLaps& lap)
	{
		frame = device.Acquire(true);
		lap("Acquire (waits for the previous present)");

		if (!frame.Target)
		{
			return completion;
		}

		const auto copied = PresentHeldFrame(frame, width, height);
		lap("Present copy (record and submit)");
		return copied;
	}

	void FrameRenderer::Impl::FinishFrame(
		const RenderSettings& settings, const FrameBuildOutputs& built, const std::array<float, 16>& viewProjection)
	{
		whiteUploaded = true;
		lutBuilt = true;

		if (settings.Environment)
		{
			builtSky = settings.Sky;
			environmentValid = true;

			if (built.EnvironmentRebuilt)
			{
				environmentBuiltTime = featureTime;
				environmentHadFeatures = built.EnvironmentWithFeatures;
			}
		}

		previousViewProjection = viewProjection;
		++frameIndex;
	}

	std::uint32_t FrameRenderer::GetSupportedMsaaSamples() const
	{
		return (device.GetCapabilities().SampleCounts & 0x1Fu) | 1u;
	}

	bool FrameRenderer::Render(const RenderFrameInput& requested)
	{
		using Clock = std::chrono::steady_clock;
		const auto cpuStart = Clock::now();
		auto& I = *impl;
		// The camera is checked here, at the boundary: a non-finite or singular one (a broken
		// input upstream) is replaced by the last valid camera, logged once per run of them.
		RenderFrameInput repaired;
		const RenderFrameInput* checked = &requested;

		if (const char* problem = CameraProblem(requested.Camera); !problem)
		{
			I.lastValidCamera = requested.Camera;
			I.cameraRejected = false;
		}
		else
		{
			if (!I.cameraRejected)
			{
				Logging::Write(Logging::Level::Warning,
					std::string("[Render] Unusable camera (") + problem + "); " +
						(I.lastValidCamera ? "keeping the last valid camera:" : "skipping frames until a valid one arrives:") +
						DescribeCamera(requested.Camera));
				I.cameraRejected = true;
			}

			if (!I.lastValidCamera)
			{
				++stats.SkippedFrames;
				stats.Presented = false;
				return false;
			}

			repaired = requested;
			repaired.Camera = *I.lastValidCamera;
			repaired.Camera.Cut = requested.Camera.Cut;
			checked = &repaired;
		}

		const RenderFrameInput& input = *checked;
		// This frame's slot: its previous frame (FramesInFlight frames ago) must be complete
		// before the slot's executor, staging and acquire semaphore are reused (GatherTimings
		// waits for it). With two slots the GPU still renders the last frame while this one
		// is built and recorded.
		device.AdvanceFrameSlot();
		settings.Sanitize();
		const float glintTime = std::chrono::duration<float>(Clock::now() - I.glintEpoch).count();
		const auto glint = Engine::PackGlint(settings.Glint, glintTime, I.GlintIndex(), I.materialSampler);
		stats.CpuPhases.clear();
		stats.FeaturePhases.clear();
		Impl::CpuLaps lap{ stats.CpuPhases, cpuStart };
		GatherTimings();
		lap("Wait for the previous frame (GPU)");

		if (!impl->pendingPrograms.empty())
		{
			impl->ApplyPrograms(lastCompletion);
		}

		// With async present the image is acquired after the frame is built (the acquire waits
		// for the previous present, which then overlaps the update and the build).
		const bool lateAcquire = device.CanAcquireLate();
		RenderDevice::Frame frame;

		if (!lateAcquire)
		{
			frame = device.Acquire();
			lap("Acquire");
		}

		const auto extent = device.GetExtent();

		if ((!lateAcquire && !frame.Valid) || extent.Width == 0 || extent.Height == 0)
		{
			++stats.SkippedFrames;
			stats.Presented = false;
			return false;
		}

		const std::uint32_t width = extent.Width;
		const std::uint32_t height = extent.Height;
		const auto& camera = input.Camera;
		// After a failed frame no history can be trusted (it may never have been written): the
		// next frame starts like a camera cut.
		const bool historyBroken = std::exchange(I.historyBroken, false);
		const bool cut = camera.Cut || historyBroken || !I.previousViewProjection;
		const bool temporalOn = settings.TemporalAntiAliasing && settings.AntiAliasing == AntiAliasingMode::Taa;
		I.ResetHistories(camera.Cut || historyBroken, temporalOn);
		const std::array<float, 2> jitter =
			temporalOn ? I.temporal->GetJitterNdc(settings.Temporal, width, height) : std::array<float, 2>{ 0.0f, 0.0f };
		const auto viewProjection = R::MultiplyRowMajor(camera.Projection, camera.View);

		R::RenderGraph graph;
		Impl::FrameBuildOutputs built;
		S::TimelinePoint completion{};

		try
		{
			completion = I.BuildAndExecute({ graph, device, frame, settings, input, stats, features, width, height, viewProjection, jitter,
											   glint, lastCompletion, lap, lateAcquire, cut, temporalOn },
				built);
		}
		catch (...)
		{
			if (built.Submitted)
			{
				// It failed after its submission: the frame runs, so it counts and is presented.
				lastCompletion = *built.Submitted;
				device.SetLastCompletion(*built.Submitted);
				I.CommitFrame(built.Pending, *built.Submitted);
				I.FinishFrame(settings, built, viewProjection);

				if (!lateAcquire)
				{
					device.Present(frame);
				}
			}
			else
			{
				// Nothing ran: importers put back what they counted as done, uploads go back to
				// their queues, histories start over, and an acquired image is still returned.
				graph.Abandon();
				I.AbortFrame(built.Pending);
				I.historyBroken = true;

				if (!lateAcquire)
				{
					I.ReturnImage(frame, width, height);
				}
			}

			throw;
		}

		auto deviceCompletion = completion;

		if (lateAcquire)
		{
			try
			{
				deviceCompletion = I.AcquireAndCopy(device, frame, completion, width, height, lap);
			}
			catch (...)
			{
				// The frame ran; only drawing it into the late image failed.
				lastCompletion = completion;
				device.SetLastCompletion(completion);
				I.CommitFrame(built.Pending, completion);
				I.FinishFrame(settings, built, viewProjection);
				I.ReturnImage(frame, width, height);
				throw;
			}
		}

		lastCompletion = completion;
		device.SetLastCompletion(deviceCompletion);
		I.CommitFrame(built.Pending, completion);
		I.FinishFrame(settings, built, viewProjection);

		lap("Commit uploads");
		stats.Presented = device.Present(frame) && frame.Valid;
		stats.SkippedFrames += frame.Valid ? 0 : 1;
		lap("Present");

		if (built.Capture && ReadCapture(device.GetExecutor(), *built.Capture, width, height, capture))
		{
			captureWidth = width;
			captureHeight = height;
		}

		I.FillFrameStats(stats, width, height);
		stats.CpuMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - cpuStart).count();
		return true;
	}

	void FrameRenderer::AddFeature(std::shared_ptr<RenderFeature> feature)
	{
		if (feature && std::find(features.begin(), features.end(), feature) == features.end())
		{
			features.push_back(std::move(feature));
		}
	}

	bool FrameRenderer::RemoveFeature(const RenderFeature* feature)
	{
		const auto found = std::find_if(features.begin(), features.end(),
			[&](const auto& entry)
			{
				return entry.get() == feature;
			});

		if (found == features.end())
		{
			return false;
		}

		impl->environmentFeatureFailures.erase(feature);
		features.erase(found);
		return true;
	}

} // namespace Engine
