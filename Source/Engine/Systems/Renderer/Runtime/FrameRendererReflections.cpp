#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	// --- Reflection probes (the local layer of the reflection hierarchy) --------
	// A few cube faces per frame (the scheduler's budget) are rendered like the main
	// view, from the probe, into small targets (lit with the global environment, so
	// probes never see each other: no recursion), resolved into the probe atlas with
	// their distances, and the probes that changed are prefiltered.
	void FrameRenderer::Impl::RecordReflectionProbes(const CaptureInputs& in, std::optional<R::ScreenSpaceFrame::ProbeInputs>& probeInputs)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& input = in.Input;
		const auto& probeSettings = in.Settings.ReflectionProbes;

		if (!I.probeRenderer || !probeSettings.Enabled || input.ReflectionProbes.empty())
		{
			return;
		}

		if (I.probeRenderer->Ensure(probeSettings.Resolution, probeSettings.MaxProbes))
		{
			I.probeScheduler.Reset();
			I.pendingFilters.clear();
			I.filteredKey.assign(I.probeRenderer->GetMaxProbes(), ~std::uint64_t(0));
		}

		const auto plan = I.probeScheduler.Update(
			input.ReflectionProbes, input.Camera.Position, I.frameIndex + 1, I.featureTime, probeSettings, input.ReflectionMovers);
		const auto atlases = I.probeRenderer->Import(graph);
		I.filteredKey.resize(I.probeRenderer->GetMaxProbes(), ~std::uint64_t(0));
		// Reflective surfaces inside this frame's captures reflect the probes as they
		// stand (before this frame's filters): reflections of reflections, one frame behind.
		std::optional<R::ReflectionCaptureProbes> captureProbes;

		if (!plan.Captures.empty())
		{
			const auto previous = ActiveProbeRecords(plan, input.ReflectionProbes);

			if (!previous.empty())
			{
				R::ReflectionCaptureProbes probes;
				probes.Records =
					graph.CreateUpload(std::as_bytes(std::span(previous)), "Reflection probe capture records", S::BufferUsage::Storage, 16);
				probes.Count = static_cast<std::uint32_t>(previous.size());
				probes.MipCount = I.probeRenderer->GetMipCount();
				captureProbes = probes;
			}
		}

		for (const auto& capture : plan.Captures)
		{
			RecordProbeCapture(in, capture, atlases, captureProbes);
		}

		RecordProbeFilters(graph, plan, input.ReflectionProbes, atlases, probeSettings.FiltersPerFrame, probeSettings.PrefilterSamples);
		graph.Export(atlases.Source, ResourceState::ShaderRead);
		graph.Export(atlases.Prefiltered, ResourceState::ShaderRead);
		in.Stats.ReflectionProbeFaces = static_cast<std::uint32_t>(plan.Captures.size());

		const auto records = ActiveProbeRecords(plan, input.ReflectionProbes);

		if (!records.empty())
		{
			R::ScreenSpaceFrame::ProbeInputs inputs;
			inputs.Cubes = atlases.Prefiltered;
			inputs.Records = graph.CreateUpload(std::as_bytes(std::span(records)), "Reflection probe records", S::BufferUsage::Storage, 16);
			inputs.ObjectId = in.Targets.ObjectId;
			inputs.Count = static_cast<std::uint32_t>(records.size());
			inputs.MipCount = I.probeRenderer->GetMipCount();
			probeInputs = inputs;
			in.Stats.ReflectionProbes = inputs.Count;
		}
	}

	// The records of the probes whose prefiltered cubes are current (filtered for their
	// present occupant): what shading samples.
	std::vector<R::GpuReflectionProbeRecord> FrameRenderer::Impl::ActiveProbeRecords(
		const R::ReflectionProbes::Plan& plan, std::span<const R::ReflectionProbeDesc> probes) const
	{
		std::vector<R::GpuReflectionProbeRecord> records;

		for (const auto& active : plan.Active)
		{
			const auto& probe = probes[active.Probe];

			if (active.Slot >= filteredKey.size() || filteredKey[active.Slot] != probe.Key)
			{
				continue; // Not prefiltered for this probe yet.
			}

			R::GpuReflectionProbeRecord record;

			for (int c = 0; c < 3; ++c)
			{
				record.PositionRadius[c] = probe.Position[c];
			}

			record.PositionRadius[3] = std::max(probe.InfluenceRadius, 1.0e-3f);
			record.Params[0] = std::max(probe.BlendDistance, 1.0e-3f);
			record.Params[1] = float(active.Slot);
			record.Params[2] = active.Age;
			record.Params[3] = float(probe.OwnerObjectId);
			records.push_back(record);
		}

		return records;
	}

	// One cube face: the probe's view through visibility, clusters and Forward+ (no
	// prepass, no transparents), resolved into the probe atlas.
	void FrameRenderer::Impl::RecordProbeCapture(const CaptureInputs& in, const R::ReflectionProbes::FaceCapture& capture,
		const R::ReflectionProbeRenderer::Atlases& atlases, const std::optional<R::ReflectionCaptureProbes>& captureProbes)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& environment = in.Environment;
		const auto& probeSettings = settings.ReflectionProbes;
		const std::uint32_t size = I.probeRenderer->GetResolution();
		const auto& probe = in.Input.ReflectionProbes[capture.Probe];
		const float nearPlane = std::max(probe.CaptureNear, 0.01f);
		const auto faceView = R::ReflectionProbes::CubeFaceView(capture.Face, probe.Position);
		const auto faceProjection = R::ReflectionProbes::CubeFaceProjection(nearPlane);
		const auto faceViewProjection = R::MultiplyRowMajor(faceProjection, faceView);
		const auto basis = R::ReflectionProbes::CubeFace(capture.Face);
		auto faceTargets = CreateForwardTargets(graph, size, size, "Probe ");
		// The view's right is the cube's mirrored axis (CubeFaceView).
		I.RecordSky(graph, settings, faceTargets, { -basis.Right[0], -basis.Right[1], -basis.Right[2] }, basis.Up, basis.Forward, size,
			size, "Probe sky");

		R::RenderViewDesc faceViewDesc;
		faceViewDesc.ViewProjection = faceViewProjection;
		faceViewDesc.CameraPosition = probe.Position;
		faceViewDesc.LodScale = float(size) * 0.5f;
		// Every face and probe is its own view: LODs chosen without the shared row
		// history, which the other views (other faces, other probes) last wrote.
		faceViewDesc.Flags = std::uint32_t(R::GpuViewFlags::ResetLodHistory);
		R::VisibilityFrameDesc faceVisibility;
		faceVisibility.View = R::BuildGpuViewRecord(faceViewDesc);
		faceVisibility.View.ExcludedObjectId = probe.OwnerObjectId; // The owner does not see itself.

		// Geometry beyond CullDistance along the face is a few texels in a probe: the
		// (infinite) far plane becomes the cull distance, and the face draws a fraction
		// of a large scene (a probe face's cost is its geometry, not its pixels).
		if (probeSettings.CullDistance > 0.0f)
		{
			const auto& f = basis.Forward;
			const float along = f[0] * probe.Position[0] + f[1] * probe.Position[1] + f[2] * probe.Position[2];
			const std::array<float, 4> far{ -f[0], -f[1], -f[2], along + probeSettings.CullDistance };

			for (int c = 0; c < 4; ++c)
			{
				faceVisibility.View.FrustumPlanes[4 * 4 + c] = far[static_cast<std::size_t>(c)];
			}
		}

		faceVisibility.IndexPages = in.IndexPages;
		faceVisibility.ReadStats = false;
		faceVisibility.ZeroUnusedCommands = R::NeedsZeroedCommands(I.drawPath);
		const auto faceVisible = I.probeVisibility->Record(graph, in.Scene, in.Geometry, faceVisibility);

		R::ClusterGridDesc faceGrid = in.Grid;
		faceGrid.ViewportWidth = size;
		faceGrid.ViewportHeight = size;
		faceGrid.Near = nearPlane;
		faceGrid.Far = std::max(settings.ClusterFar, nearPlane * 2.0f);
		R::ClusterView faceClusterView;
		faceClusterView.View = faceView;
		faceClusterView.Projection = faceProjection;
		const auto faceClusters = I.clusters->Record(graph, in.Lights, faceGrid, faceClusterView);

		R::ForwardPlusFrame faceFrame = in.ForwardFrame;
		faceFrame.External.clear(); // Feature geometry is culled for the main view only.
		faceFrame.BeforeOpaque = {};
		faceFrame.BeforeTransparent = {};
		faceFrame.Visibility = &faceVisible;
		faceFrame.Clusters = &faceClusters;
		faceFrame.View.ViewProjection = faceViewProjection;
		faceFrame.View.PreviousViewProjection = faceViewProjection;
		faceFrame.View.Jitter = { 0.0f, 0.0f };
		faceFrame.View.CameraPosition = probe.Position;
		faceFrame.View.CameraForward = basis.Forward;
		faceFrame.View.DebugMode = R::ForwardPlusDebugMode::None;
		faceFrame.Transparent = false;			// Glass is not worth a sort per captured face.
		faceFrame.DepthPrepass = false;			// One geometry pass: small targets, cheap overdraw.
		faceFrame.DebugName = "Probe Forward+"; // Its own rows in the GPU timings.
		I.forward->Record(graph, faceFrame, faceTargets);
		R::ReflectionProbeRenderer::CaptureSky captureSky;

		if (environment && settings.SkyBackground)
		{
			captureSky.Environment = environment->Prefiltered;
			captureSky.Scale = settings.EnvironmentIntensity;
			captureSky.Rotation = settings.EnvironmentRotation;
		}

		R::ReflectionCaptureTargets resolveTargets;
		resolveTargets.Color = faceTargets.Color;
		resolveTargets.Depth = faceTargets.Depth;
		resolveTargets.Normal = *faceTargets.Normal;
		resolveTargets.Reflectance = *faceTargets.Reflectance;
		resolveTargets.Specular = *faceTargets.Specular;
		resolveTargets.ObjectId = faceTargets.ObjectId;
		I.probeRenderer->RecordResolve(
			graph, atlases, capture.Slot, capture.Face, resolveTargets, probe.Position, nearPlane, captureSky, captureProbes);
	}

	// Prefilter a bounded number of probes a frame (each is 6 faces x every mip): probes not
	// yet filtered for their current occupant first, then the ones whose faces changed
	// longest ago.
	void FrameRenderer::Impl::RecordProbeFilters(R::RenderGraph& graph, const R::ReflectionProbes::Plan& plan,
		std::span<const R::ReflectionProbeDesc> probes, const R::ReflectionProbeRenderer::Atlases& atlases, std::size_t filtersPerFrame,
		std::uint32_t prefilterSamples)
	{
		for (const auto slot : plan.Filter)
		{
			if (std::find(pendingFilters.begin(), pendingFilters.end(), slot) == pendingFilters.end())
			{
				pendingFilters.push_back(slot);
			}
		}

		const auto keyOf = [&](std::uint32_t slot) -> std::uint64_t
		{
			for (const auto& active : plan.Active)
			{
				if (active.Slot == slot)
				{
					return probes[active.Probe].Key;
				}
			}

			return ~std::uint64_t(0);
		};
		const auto slotUnfiltered = [&](std::uint32_t slot, std::uint64_t key)
		{
			return slot < filteredKey.size() && filteredKey[slot] != key;
		};
		std::stable_sort(pendingFilters.begin(), pendingFilters.end(),
			[&](std::uint32_t a, std::uint32_t b)
			{
				const bool urgentA = slotUnfiltered(a, keyOf(a));
				const bool urgentB = slotUnfiltered(b, keyOf(b));
				return urgentA && !urgentB;
			});
		// Every probe captured this frame is filtered this frame (a captured face that waits
		// for its filter shows the old content: a moving object lags, or doubles across
		// faces filtered at different times), plus FiltersPerFrame of any backlog.
		const auto budget = static_cast<std::uint32_t>(std::max<std::size_t>(filtersPerFrame, 1u) + plan.Filter.size());
		std::uint32_t filtered = 0;

		while (!pendingFilters.empty() && filtered < budget)
		{
			const std::uint32_t slot = pendingFilters.front();
			pendingFilters.erase(pendingFilters.begin());

			if (slot >= filteredKey.size())
			{
				continue;
			}

			probeRenderer->RecordFilter(graph, atlases, slot, prefilterSamples);
			filteredKey[slot] = keyOf(slot);
			++filtered;
		}
	}

	// --- Planar reflections (the sharp top of the reflection hierarchy) ----------
	// A few captures per frame (the planner's budget), each a small Forward+ render from
	// the camera mirrored in the reflector, through it as a window: the off-axis frustum
	// spans only the reflector's visible portal and its near plane is the mirror, so GPU
	// visibility culls everything else (and the reflector itself) at coarse LODs. The
	// captures land in a persistent atlas the composite reprojects until they are re-rendered.
	void FrameRenderer::Impl::RecordPlanarReflections(const CaptureInputs& in, std::uint32_t width, std::uint32_t height,
		const std::array<float, 16>& viewProjection, const std::optional<R::ScreenSpaceFrame::ProbeInputs>& probeInputs,
		std::optional<R::ScreenSpaceFrame::PlanarInputs>& planarInputs)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& input = in.Input;
		const auto& camera = input.Camera;
		auto& stats = in.Stats;
		const auto& indexPages = in.IndexPages;
		const auto& geometryResources = in.Geometry;
		const auto& sceneResources = in.Scene;
		const auto& lightResources = in.Lights;
		const auto& environment = in.Environment;
		const auto& targets = in.Targets;
		const auto& grid = in.Grid;
		const auto& forwardFrame = in.ForwardFrame;
		const auto& planarSettings = settings.PlanarReflections;

		if (I.planarRenderer && planarSettings.Enabled && !input.PlanarReflectors.empty())
		{
			if (I.planarRenderer->Ensure(planarSettings.AtlasResolution, planarSettings.MaxPlanes))
			{
				I.planarPlanner.Reset();
			}

			R::PlanarReflections::ViewCamera planarCamera;
			planarCamera.ViewProjection = viewProjection;
			planarCamera.Position = camera.Position;
			planarCamera.Forward = camera.Forward;
			planarCamera.VerticalFov = camera.VerticalFov;
			planarCamera.ViewportWidth = float(width);
			planarCamera.ViewportHeight = float(height);
			const auto& plan = I.planarPlanner.Update(
				input.PlanarReflectors, planarCamera, I.frameIndex + 1, I.featureTime, planarSettings, input.ReflectionMovers);
			stats.PlanarCandidates = plan.Candidates;

			if (!plan.Captures.empty() || !plan.Records.empty())
			{
				const auto atlas = I.planarRenderer->Import(graph);
				// Reflective surfaces inside the captures reflect the probes (this frame's).
				std::optional<R::PlanarCaptureProbes> captureProbes;

				if (probeInputs)
				{
					R::PlanarCaptureProbes probes;
					probes.Cubes = probeInputs->Cubes;
					probes.Records = probeInputs->Records;
					probes.Count = probeInputs->Count;
					probes.MipCount = probeInputs->MipCount;
					captureProbes = probes;
				}

				for (const auto& capture : plan.Captures)
				{
					// Rendered supersampled (RenderWidth x RenderHeight), averaged down by the resolve.
					const std::uint32_t renderWidth = capture.RenderWidth;
					const std::uint32_t renderHeight = capture.RenderHeight;
					auto captureTargets = CreateForwardTargets(graph, renderWidth, renderHeight, "Planar ");
					// Sky rays of the off-axis view: forward shifted to the window's centre.
					const auto& f = capture.FrustumScale;
					std::array<float, 3> rayRight{}, rayUp{}, rayForward{};

					for (int c = 0; c < 3; ++c)
					{
						rayRight[c] = capture.Right[c] / f[0];
						rayUp[c] = capture.Up[c] / f[1];
						rayForward[c] = capture.Forward[c] + capture.Right[c] * (f[2] / f[0]) + capture.Up[c] * (f[3] / f[1]);
					}

					I.RecordSky(graph, settings, captureTargets, rayRight, rayUp, rayForward, renderWidth, renderHeight, "Planar sky");

					R::RenderViewDesc captureViewDesc;
					captureViewDesc.ViewProjection = capture.ViewProjection;
					captureViewDesc.CameraPosition = capture.Position;
					// LODs follow the stored texels (what the reflection shows), not the supersampling.
					captureViewDesc.LodScale = capture.LodScale;
					// One LOD history serves every capture of the frame, each from its own
					// view: hysteresis against the last capture's choice made the terrain's
					// LOD flip from frame to frame, and its coarse chunks rose through the
					// lab's floor pad in sections (sand patches flickering in the spheres'
					// lower halves). Each capture picks its LODs from its own view only.
					captureViewDesc.Flags = std::uint32_t(R::GpuViewFlags::ResetLodHistory);
					R::VisibilityFrameDesc captureVisibility;
					captureVisibility.View = R::BuildGpuViewRecord(captureViewDesc);
					captureVisibility.View.ExcludedObjectId = capture.ExcludedObjectId; // The mirror does not see itself.

					// The (degenerate, infinite) far plane becomes the reflection's cull distance.
					for (int c = 0; c < 4; ++c)
					{
						captureVisibility.View.FrustumPlanes[4 * 4 + c] = capture.CullPlane[c];
					}

					captureVisibility.IndexPages = indexPages;
					captureVisibility.ReadStats = false;
					captureVisibility.ZeroUnusedCommands = R::NeedsZeroedCommands(I.drawPath);
					const auto captureVisible = I.planarVisibility->Record(graph, sceneResources, geometryResources, captureVisibility);

					R::ClusterGridDesc captureGrid = grid;
					captureGrid.ViewportWidth = renderWidth;
					captureGrid.ViewportHeight = renderHeight;
					captureGrid.Near = capture.NearClip;
					captureGrid.Far = std::max(std::min(settings.ClusterFar, planarSettings.CullDistance), capture.NearClip * 2.0f);
					R::ClusterView captureClusterView;
					captureClusterView.View = capture.View;
					captureClusterView.Projection = capture.Projection;
					const auto captureClusters = I.clusters->Record(graph, lightResources, captureGrid, captureClusterView);

					R::ForwardPlusFrame captureFrame = forwardFrame;
					captureFrame.External.clear(); // Feature geometry is culled for the main view only.
					captureFrame.BeforeOpaque = {};
					captureFrame.BeforeTransparent = {};
					captureFrame.Visibility = &captureVisible;
					captureFrame.Clusters = &captureClusters;
					captureFrame.View.ViewProjection = capture.ViewProjection;
					captureFrame.View.PreviousViewProjection = capture.ViewProjection;
					// No jitter: a capture texel covers several screen pixels once a curved
					// mirror magnifies it, so a sub-texel jitter became a visible wobble of
					// distant (small) reflected objects that TAA cannot reproject (its motion
					// follows the mirror, not the reflection). The Catmull-Rom fetch smooths
					// the texels instead.
					captureFrame.View.Jitter = { 0.0f, 0.0f };
					captureFrame.View.CameraPosition = capture.Position;
					captureFrame.View.CameraForward = capture.Forward;
					captureFrame.View.DebugMode = R::ForwardPlusDebugMode::None;
					captureFrame.Transparent = false;			// No sort per capture.
					captureFrame.DepthPrepass = false;			// One geometry pass (below screen resolution).
					captureFrame.DebugName = "Planar Forward+"; // Its own rows in the GPU timings.
					I.forward->Record(graph, captureFrame, captureTargets);
					R::ReflectionProbeCaptureSky captureSky;

					if (environment && settings.SkyBackground)
					{
						captureSky.Environment = environment->Prefiltered;
						captureSky.Scale = settings.EnvironmentIntensity;
						captureSky.Rotation = settings.EnvironmentRotation;
					}

					R::PlanarCaptureTargets resolveTargets;
					resolveTargets.Color = captureTargets.Color;
					resolveTargets.Depth = captureTargets.Depth;
					resolveTargets.Normal = *captureTargets.Normal;
					resolveTargets.Reflectance = *captureTargets.Reflectance;
					resolveTargets.Specular = *captureTargets.Specular;
					resolveTargets.ObjectId = captureTargets.ObjectId;
					I.planarRenderer->RecordResolve(graph, atlas, capture, resolveTargets, captureSky, captureProbes);
				}

				graph.Export(atlas.Texture, ResourceState::ShaderRead);
				stats.PlanarCaptures = static_cast<std::uint32_t>(plan.Captures.size());

				if (!plan.Records.empty())
				{
					R::ScreenSpaceFrame::PlanarInputs inputs;
					inputs.Atlas = atlas.Texture;
					inputs.Records = graph.CreateUpload(
						std::as_bytes(std::span(plan.Records)), "Planar reflection records", S::BufferUsage::Storage, 16);
					inputs.ObjectId = targets.ObjectId;
					inputs.Count = static_cast<std::uint32_t>(plan.Records.size());
					planarInputs = inputs;
					stats.PlanarReflections = inputs.Count;
				}
			}
		}
	}

} // namespace Engine
