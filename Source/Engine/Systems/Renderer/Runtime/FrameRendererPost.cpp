#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"
#include "Engine/Logging/Log.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	// SSAO, SSR and their composite, with the reflection histories they read and keep.
	R::ScreenSpaceGraphResources FrameRenderer::Impl::RecordScreenSpace(const ScreenSpaceInputs& in)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& camera = in.Camera;
		const auto width = in.Width;
		const auto height = in.Height;
		const auto& jitter = in.Jitter;
		const bool temporalOn = in.Temporal;
		const bool draw3D = in.Draw3D;
		const auto& targets = in.Targets;
		const auto& probeInputs = in.Probes;
		const auto& planarInputs = in.Planar;
		R::ScreenSpaceFrame ssFrame;
		ssFrame.Color = targets.Color;
		ssFrame.Depth = targets.Depth;
		ssFrame.Normal = *targets.Normal;
		ssFrame.Indirect = *targets.Indirect;
		ssFrame.Reflectance = targets.Reflectance;
		ssFrame.Specular = targets.Specular;
		ssFrame.View.View = camera.View;
		ssFrame.View.Projection = camera.Projection;
		ssFrame.View.Jitter = jitter;
		ssFrame.Settings = settings.ScreenSpace;

		if (!draw3D)
		{
			ssFrame.Settings.AmbientOcclusion.Enabled = false;
			ssFrame.Settings.Reflections.Enabled = false;
		}

		// Without a temporal resolve a moving noise pattern only crawls: hold it still.
		ssFrame.NoiseFrame = temporalOn ? static_cast<std::uint32_t>(I.frameIndex) : 0u;
		ssFrame.BackDepth = targets.BackDepth;
		ssFrame.Probes = probeInputs;
		ssFrame.Planar = planarInputs;
		// Reflections of reflections: rays read last frame's finished screen-space colour
		// (which holds its reflections, before TAA) at the hit's reprojected position.
		const bool ssrColorHistory =
			ssFrame.Settings.Reflections.Enabled && ssFrame.Settings.Reflections.History && targets.Velocity.has_value();

		for (auto& texture : I.ssrColorHistory)
		{
			if (ssrColorHistory && (!texture || texture->GetDesc().Extent.Width != width || texture->GetDesc().Extent.Height != height))
			{
				S::TextureDesc historyDesc;
				historyDesc.Extent = { width, height, 1 };
				historyDesc.PixelFormat = S::Format::RGBA16Float;
				historyDesc.Usage = S::TextureUsage::Sampled | S::TextureUsage::TransferDestination;
				historyDesc.DebugName = "Reflection colour history";
				texture = I.device.CreateTexture(historyDesc);
				I.ssrColorValid = false;
			}
		}

		if (ssrColorHistory && I.ssrColorValid)
		{
			ssFrame.History = graph.ImportTexture(*I.ssrColorHistory[I.ssrColorLatest], ResourceState::ShaderRead);
			ssFrame.Velocity = *targets.Velocity;
		}

		if (targets.Velocity)
		{
			ssFrame.Velocity = *targets.Velocity;
		}

		// The temporal reflection filter: this frame writes one history texture while the
		// other holds last frame's.
		std::optional<R::GraphTexture> reflectionHistoryNext;
		const bool reflectionTemporal = ssFrame.Settings.Reflections.Temporal && targets.Velocity &&
			(ssFrame.Settings.Reflections.Enabled || probeInputs.has_value() || planarInputs.has_value());

		if (reflectionTemporal)
		{
			for (auto& texture : I.reflectionHistory)
			{
				if (!texture || texture->GetDesc().Extent.Width != width || texture->GetDesc().Extent.Height != height)
				{
					S::TextureDesc historyDesc;
					historyDesc.Extent = { width, height, 1 };
					historyDesc.PixelFormat = S::Format::RGBA16Float;
					historyDesc.Usage = S::TextureUsage::Sampled | S::TextureUsage::Storage;
					historyDesc.DebugName = "Reflection history";
					texture = I.device.CreateTexture(historyDesc);
					I.reflectionHistoryValid = false;
				}
			}

			if (I.reflectionHistory[0] && I.reflectionHistory[1])
			{
				R::ScreenSpaceFrame::ReflectionHistoryInputs history;
				const auto next = 1u - I.reflectionHistoryLatest;

				if (I.reflectionHistoryValid)
				{
					history.Previous = graph.ImportTexture(*I.reflectionHistory[I.reflectionHistoryLatest], ResourceState::ShaderRead);
				}

				history.Next = graph.ImportTexture(*I.reflectionHistory[next], ResourceState::Undefined);
				history.Blend = ssFrame.Settings.Reflections.TemporalBlend;
				ssFrame.ReflectionTemporal = history;
				reflectionHistoryNext = history.Next;
			}
		}

		auto screen = I.screenSpace->Record(graph, ssFrame);

		if (reflectionHistoryNext && !screen.Passthrough)
		{
			graph.Export(*reflectionHistoryNext, ResourceState::ShaderRead);
			I.reflectionHistoryLatest = 1u - I.reflectionHistoryLatest;
			I.reflectionHistoryValid = true;
		}
		else
		{
			I.reflectionHistoryValid = false;
		}

		// Keep this frame's finished screen-space colour for next frame's reflections.
		if (ssrColorHistory && I.ssrColorHistory[0] && I.ssrColorHistory[1] &&
			graph.GetDesc(screen.Output).PixelFormat == S::Format::RGBA16Float)
		{
			const auto next = 1u - I.ssrColorLatest;
			const auto source = screen.Output;
			const auto destination = graph.ImportTexture(*I.ssrColorHistory[next], ResourceState::Undefined);
			graph.AddPass(
				"Reflection colour history", S::QueueType::Graphics,
				[&](R::RenderGraphBuilder& b)
				{
					b.Read(source, ResourceState::CopySource);
					b.Write(destination, ResourceState::CopyDestination);
				},
				[source, destination, width, height](R::RenderCommandContext& c)
				{
					S::TextureCopyRegion region;
					region.Extent = { width, height, 1 };
					c.Commands().CopyTexture(c.Get(source), c.Get(destination), region);
				});
			graph.Export(destination, ResourceState::ShaderRead);
			I.ssrColorLatest = next;
			I.ssrColorValid = true;
		}
		else
		{
			I.ssrColorValid = false;
		}
		return screen;
	}

	// Render features (gameplay-added passes) run at three stages of the frame.
	R::GraphTexture FrameRenderer::Impl::RunFeatures(const SceneInputs& in, RenderFeatureStage stage, R::GraphTexture color)
	{
		using Clock = std::chrono::steady_clock;
		auto& I = *this;
		const auto& settings = in.Settings;
		const auto& features = in.Features;
		auto& stats = in.Stats;
		std::vector<RenderFeature*> ordered;

		for (const auto& feature : features)
		{
			if (feature && feature->Enabled && feature->GetStage() == stage && (settings.CinematicEffects || !feature->IsCinematic()))
			{
				ordered.push_back(feature.get());
			}
		}

		if (ordered.empty() || !in.Draw3D)
		{
			return color;
		}

		std::stable_sort(ordered.begin(), ordered.end(),
			[](const RenderFeature* a, const RenderFeature* b)
			{
				return a->GetOrder() < b->GetOrder();
			});
		RenderFeatureContext context(in.Graph, stage, in.FeatureView, settings, color, in.Targets.Depth, I.FeatureServices());

		for (auto* feature : ordered)
		{
			// A broken feature (missing program, wrong bindings) is switched off with a
			// log line instead of failing every frame.
			try
			{
				const auto featureStart = Clock::now();
				feature->Record(context);
				stats.FeaturePhases.push_back({ std::string(feature->GetName()) + ": record",
					std::chrono::duration<double, std::milli>(Clock::now() - featureStart).count() });
			}
			catch (const std::exception& error)
			{
				feature->Enabled = false;
				std::cerr << "[Render] Feature '" << feature->GetName() << "' disabled: " << error.what() << '\n';
			}
		}

		return context.Color();
	}

	// Features, particles, TAA, post, screen anti-aliasing and the display-referred features.
	R::GraphTexture FrameRenderer::Impl::RecordPost(
		const SceneInputs& in, R::GraphTexture sceneOutput, const std::optional<R::ParticleGraphResources>& particles)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& input = in.Input;
		const auto& featureView = in.FeatureView;
		const auto& targets = in.Targets;
		const bool temporalOn = in.Temporal;
		const bool draw3D = in.Draw3D;
		R::GraphTexture sceneColor = RunFeatures(in, RenderFeatureStage::BeforeTemporal, sceneOutput);
		// Particles blend over the finished scene: drawn into the Forward+ color, the
		// screen-space composite re-fogged them with the depth behind them and the clouds
		// were composited over them wherever the sky showed through, so near the horizon
		// (thick fog, long cloud paths) whole bands of a fountain vanished. They still
		// depth-test against the scene and are resolved by TAA like everything else.
		if (particles)
		{
			const R::ParticleRenderProgram program{ I.particleAdditive.get(), I.particleAlpha.get(), I.particleRender.Layout.get() };
			I.particles->Draw(graph, *particles, program, { sceneColor, targets.Depth }, I.bindless->GetTable());
		}

		R::GraphTexture resolved = sceneColor;

		if (temporalOn)
		{
			R::TemporalFrame temporalFrame;
			temporalFrame.Color = sceneColor;
			temporalFrame.Depth = targets.Depth;
			temporalFrame.Velocity = *targets.Velocity;
			temporalFrame.Settings = settings.Temporal;
			resolved = I.temporal->Record(graph, temporalFrame).Output;
		}

		resolved = RunFeatures(in, RenderFeatureStage::BeforePostProcess, resolved);

		auto postOutput = CreateTarget(graph, in.Width, in.Height, "", S::Format::RGBA8Unorm,
			S::TextureUsage::Storage | S::TextureUsage::ColorAttachment | S::TextureUsage::Sampled | S::TextureUsage::TransferSource,
			"Frame");
		R::PostProcessFrame postFrame;
		postFrame.Source = resolved;
		postFrame.Output = postOutput;
		postFrame.Settings = settings.Post;
		postFrame.Settings.Output.Encoding = R::OutputEncoding::Srgb;
		postFrame.DeltaTime = std::clamp(std::isfinite(input.DeltaTime) ? input.DeltaTime : 0.0f, 0.0f, 1.0f);
		I.post->Record(graph, postFrame);

		if (draw3D &&
			(settings.AntiAliasing == AntiAliasingMode::Fxaa || settings.AntiAliasing == AntiAliasingMode::Smaa1x ||
				settings.Sharpening > 0.0f))
		{
			postOutput =
				RecordScreenAntiAliasing(graph, featureView, settings, postOutput, targets.Depth, targets.Normal, I.FeatureServices());
		}

		{
			// Display-referred features write their own RGBA8 target; the UI and the
			// presentation then use it (it keeps the Frame target's usages).
			const auto finalColor = RunFeatures(in, RenderFeatureStage::AfterPostProcess, postOutput);

			if (finalColor != postOutput)
			{
				postOutput = finalColor;
			}
		}

		// The screen blur (menu backdrops) last, so it blurs everything the UI is drawn over
		// (or RecordUi runs it between UI layers).
		in.Stats.BlurLevels = ScreenBlurLevels(settings, featureView.Height);

		if (in.Stats.BlurLevels > 0 && !settings.Blur.AfterUiLayer)
		{
			postOutput = RecordScreenBlur(graph, featureView, settings, postOutput, I.FeatureServices());
		}

		return postOutput;
	}

	// Scene viewports (UI images) and the UI documents, drawn over target.
	R::GraphTexture FrameRenderer::Impl::RecordUi(
		const SceneInputs& in, R::GraphTexture target, const S::TimelinePoint& lastCompletion, bool& atlasPending, bool& viewportsPending)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& input = in.Input;
		const auto& targets = in.Targets;
		const auto& glint = in.Glint;
		auto& stats = in.Stats;
		std::vector<R::GraphTexture> viewportTextures;

		if (I.viewports->GetStats().Viewports > 0)
		{
			viewportTextures = I.viewports->Record(graph, *I.geometry, in.Geometry, *I.residency,
				{ I.sceneViewportPipeline.get(), I.sceneViewport.Layout.get(), I.sceneViewportTranslucentPipeline.get() },
				I.bindless->GetTable(), I.materialSampler,
				I.pointMaterialSampler);
			viewportsPending = true;
		}

		stats.UiQuads = 0;
		// The screen blur between UI layers (RecordPost ran it already when AfterUiLayer is unset).
		bool blurPending = stats.BlurLevels > 0 && settings.Blur.AfterUiLayer.has_value();
		const auto blurBefore = [&](std::int32_t layer)
		{
			if (blurPending && layer > *settings.Blur.AfterUiLayer)
			{
				target = RecordScreenBlur(graph, in.FeatureView, settings, target, I.FeatureServices());
				blurPending = false;
			}
		};

		if (settings.Ui && !input.Ui.empty() && input.GlyphAtlas)
		{
			if (I.attachedAtlas && I.attachedAtlas != input.GlyphAtlas)
			{
				I.atlasTextures->Release(lastCompletion);
			}

			I.attachedAtlas = input.GlyphAtlas;
			const auto atlasFrame = I.atlasTextures->Update(graph, *input.GlyphAtlas);
			atlasPending = true;
			const R::UiRenderProgram program{ I.uiPipeline.get(), I.uiQuad.Layout.get(), I.uiDepthPipeline.get(), I.uiInvertPipeline.get(),
				I.uiInvertDepthPipeline.get() };

			// Layer of a quad: its item's UiDrawItem::Layer plus the node's UiStyle::Layer. Layers
			// draw in ascending order, items in list order within a layer.
			struct Painted
			{
				const UiDrawItem* Item = nullptr;
				std::span<const Swim::UI::UiPaintQuad> Paint;
				bool Layered = false; // Some quad has a node layer.
			};

			std::vector<Painted> painted;
			std::vector<std::int32_t> layers;

			for (const auto& item : input.Ui)
			{
				if (!item.Document || item.Opacity <= 0.0f)
				{
					continue;
				}

				auto& document = *item.Document;
				document.EnsureLayout();
				Painted entry{ &item, document.Paint(*input.GlyphAtlas) };
				layers.push_back(item.Layer);

				for (const auto& quad : entry.Paint)
				{
					if (quad.Layer != 0)
					{
						entry.Layered = true;
						layers.push_back(item.Layer + quad.Layer);
					}
				}

				painted.push_back(entry);
			}

			std::sort(layers.begin(), layers.end());
			layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
			std::vector<Swim::UI::UiPaintQuad> subset;

			for (const auto layer : layers)
			{
				blurBefore(layer);

				for (const auto& entry : painted)
				{
					const auto& item = *entry.Item;
					auto paint = entry.Paint;

					if (entry.Layered)
					{
						subset.clear();

						for (const auto& quad : entry.Paint)
						{
							if (item.Layer + quad.Layer == layer)
							{
								subset.push_back(quad);
							}
						}

						paint = subset;
					}
					else if (item.Layer != layer)
					{
						continue;
					}

					if (paint.empty())
					{
						continue;
					}

					R::UiRenderFrame uiFrame;
					uiFrame.Paint = paint;
					uiFrame.Target = target;
					uiFrame.DpiScale = item.DpiScale;
					uiFrame.OffsetX = item.ClipFromCanvas ? 0.0f : item.OffsetX;
					uiFrame.OffsetY = item.ClipFromCanvas ? 0.0f : item.OffsetY;
					uiFrame.ClipFromCanvas = item.ClipFromCanvas;

					if (item.ClipFromCanvas && item.DepthTest)
					{
						uiFrame.Depth = targets.Depth;
					}

					uiFrame.Opacity = std::clamp(item.Opacity, 0.0f, 1.0f);
					uiFrame.Atlas = &atlasFrame;
					uiFrame.Images = viewportTextures;
					uiFrame.Composition.Encoding = R::UiOutputEncoding::Srgb;
					uiFrame.Glint.Mode = glint.Source[0];
					uiFrame.Glint.Color = glint.ColorA;
					uiFrame.Glint.Texture = glint.Source[1];
					uiFrame.Glint.Sampler = glint.Source[2];
					uiFrame.Glint.Time = glint.Motion[3] * glint.Extra[3];

					if (I.ui->Record(graph, uiFrame, program, I.bindless->GetTable()))
					{
						stats.UiQuads += I.ui->GetStats().Quads;
					}
				}
			}
		}

		if (blurPending)
		{
			target = RecordScreenBlur(graph, in.FeatureView, settings, target, I.FeatureServices());
		}

		return target;
	}

	// The frame into the swapchain image, or the slot's held copy when the image is acquired late.
	void FrameRenderer::Impl::RecordPresentation(R::RenderGraph& graph, const RenderDevice::Frame& frame, bool lateAcquire,
		R::GraphTexture postOutput, std::uint32_t width, std::uint32_t height)
	{
		if (lateAcquire)
		{
			// Built before the image is acquired: the frame goes to this slot's held copy,
			// and a second, small submission draws it into the image once acquired (the
			// acquire waits for the previous present, which can block until its GPU work
			// is done; meanwhile this frame's GPU work already runs).
			auto& held = HeldFrame(renderDevice.GetFrameSlot(), width, height);
			const auto target = graph.ImportTexture(*held.Texture, held.State);
			AddPresentPass(graph, postOutput, target, GetPresentPipeline(S::Format::RGBA8Unorm), *present.Layout, *presentSampler, false,
				width, height);
			graph.Export(target, ResourceState::ShaderRead);
			graph.OnAbandoned(
				[this, slot = renderDevice.GetFrameSlot(), state = held.State]
				{
					heldFrames[slot].State = state;
				});
			held.State = ResourceState::ShaderRead;
		}
		else if (frame.Target)
		{
			const auto surface = graph.ImportTexture(*frame.Target, frame.Presented ? ResourceState::Present : ResourceState::Undefined);
			const auto format = renderDevice.GetSwapchainFormat();
			AddPresentPass(graph, postOutput, surface, GetPresentPipeline(format), *present.Layout, *presentSampler, IsSrgbFormat(format),
				width, height);
			graph.Export(surface, ResourceState::Present);
		}
		else
		{
			// Headless: keep the frame alive to the end of the graph.
			graph.Export(postOutput, ResourceState::ShaderRead);
		}
	}

	// An acquired image whose frame failed goes back to the swapchain cleared: every acquired
	// image must be presented before the swapchain can be replaced.
	void FrameRenderer::Impl::ReturnImage(const RenderDevice::Frame& frame, std::uint32_t width, std::uint32_t height)
	{
		if (!frame.Valid || !frame.Target)
		{
			return;
		}

		// Called while a failed frame is handled: a second failure here must not replace the first.
		try
		{
		R::RenderGraph graph;
		const auto surface = graph.ImportTexture(*frame.Target, frame.Presented ? ResourceState::Present : ResourceState::Undefined);
		graph.AddPass(
			"Present (failed frame)", S::QueueType::Graphics,
			[&](R::RenderGraphBuilder& b)
			{
				b.Write(surface, ResourceState::ColorAttachment);
			},
			[surface, width, height](R::RenderCommandContext& c)
			{
				S::RenderingAttachmentDesc attachment{};
				attachment.View = &c.CreateView(surface);
				attachment.Load = S::LoadOp::Clear;
				auto& list = c.Commands();
				list.BeginRendering({ { &attachment, 1 }, nullptr, { width, height } });
				list.EndRendering();
			});
		graph.Export(surface, ResourceState::Present);
		renderDevice.SetLastCompletion(renderDevice.GetExecutor().Execute(graph.Compile(), renderDevice.GetSubmit(frame)));
		renderDevice.Present(frame);
		}
		catch (const std::exception& error)
		{
			Logging::Write(Logging::Level::Error, std::string("[Render] Could not return a failed frame's image: ") + error.what());
		}
	}

	// The held frame into the acquired image (a second, small submission).
	S::TimelinePoint FrameRenderer::Impl::PresentHeldFrame(const RenderDevice::Frame& frame, std::uint32_t width, std::uint32_t height)
	{
		auto& held = heldFrames[renderDevice.GetFrameSlot()];
		R::RenderGraph presentGraph;
		const auto source = presentGraph.ImportTexture(*held.Texture, held.State);
		const auto surface = presentGraph.ImportTexture(*frame.Target, frame.Presented ? ResourceState::Present : ResourceState::Undefined);
		const auto format = renderDevice.GetSwapchainFormat();
		AddPresentPass(presentGraph, source, surface, GetPresentPipeline(format), *present.Layout, *presentSampler, IsSrgbFormat(format),
			width, height);
		presentGraph.Export(surface, ResourceState::Present);
		presentGraph.Export(source, ResourceState::ShaderRead);
		return renderDevice.GetPresentExecutor().Execute(presentGraph.Compile(presentGraphCache), renderDevice.GetSubmit(frame));
	}

} // namespace Engine
