#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	namespace
	{

		// Features that draw into the geometry passes prepare once per frame, after the
		// shadow plan (they cull against its tiles) and before any geometry pass.
		struct GeometryShared
		{
			RenderFeatureView View;
			RenderFeatureContext::Services Services;
		};

		bool OccludesGeometry(const std::shared_ptr<RenderFeature>& feature)
		{
			return feature && feature->Enabled && (feature->GetGeometryPasses() & RenderGeometryAllPasses) != 0 && feature->UsesOcclusion();
		}

		bool CopiesScene(const R::ExternalGeometryDraws& draws)
		{
			return draws.SceneCopy && (draws.Passes & RenderGeometryPassBit(RenderGeometryPass::Transparent)) != 0;
		}

	} // namespace

	RenderFeatureContext::Services FrameRenderer::Impl::FeatureServices()
	{
		RenderFeatureContext::Services services;
		services.LoadCompute = [this](std::string_view name) -> const RuntimeComputeProgram&
		{
			auto& slot = featurePrograms[std::string(name)];

			if (!slot)
			{
				slot = std::make_unique<RuntimeComputeProgram>(shaders.LoadCompute(name));
			}

			return *slot;
		};
		services.GetSampler = [this](std::string_view kind) -> S::Sampler&
		{
			if (kind == "LinearRepeat")
			{
				return *linearRepeat;
			}

			if (kind == "PointClamp")
			{
				return *presentSampler;
			}

			return *linearClamp;
		};
		services.Device = &device;
		services.GetGeometryPipeline = [this](std::string_view name, RenderGeometryPass pass, bool prepassed,
										   const RenderGeometryState& state, S::SampleCount samples)
		{
			return GetGeometryPipeline(name, pass, prepassed, state, samples);
		};
		services.OverrideConstants = overrideConstants;
		return services;
	}

	void FrameRenderer::Impl::PrepareFeatureGeometry(const SceneInputs& in, FeatureGeometry& geometry)
	{
		using Clock = std::chrono::steady_clock;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& featureView = in.FeatureView;
		auto& stats = in.Stats;
		const auto& features = in.Features;
		geometry.Prepared = true;
		auto services = FeatureServices();
		services.ShadowViews = geometry.ShadowViews;
		RenderFeatureContext context(graph, RenderFeatureStage::BeforeTemporal, featureView, settings, {}, {}, services);
		services.ShadowViews = {};
		const auto shared = std::make_shared<const GeometryShared>(GeometryShared{ featureView, std::move(services) });

		for (const auto& feature : features)
		{
			const auto passes = feature && feature->Enabled ? feature->GetGeometryPasses() & RenderGeometryAllPasses : 0u;

			if (passes == 0)
			{
				continue;
			}

			try
			{
				const auto featureStart = Clock::now();
				auto resources = feature->PrepareGeometry(context);
				stats.FeaturePhases.push_back({ std::string(feature->GetName()) + ": geometry",
					std::chrono::duration<double, std::milli>(Clock::now() - featureStart).count() });
				R::ExternalGeometryDraws draws;
				draws.Passes = passes;
				draws.Buffers = std::move(resources.Buffers);
				draws.IndirectBuffers = std::move(resources.IndirectBuffers);
				draws.Textures = std::move(resources.Textures);
				draws.SceneCopy = resources.SceneCopy;
				draws.StaticShadow = resources.StaticShadow;
				draws.ShadowGeneration = resources.ShadowGeneration;
				draws.Draw = [feature, shared, passes](R::ExternalDrawContext& draw)
				{
					if (!feature->Enabled)
					{
						return;
					}

					// Only depth this feature wrote itself counts as prepassed.
					auto local = draw;
					local.DepthPrepassed = draw.DepthPrepassed && (passes & RenderGeometryPassBit(RenderGeometryPass::DepthPrepass)) != 0;
					RenderFeatureDrawContext drawContext(local, shared->View, shared->Services);

					try
					{
						feature->DrawGeometry(drawContext);
					}
					catch (const std::exception& error)
					{
						feature->Enabled = false;
						std::cerr << "[Render] Feature '" << feature->GetName() << "' disabled: " << error.what() << '\n';
					}
				};
				geometry.Draws.push_back(std::move(draws));
			}
			catch (const std::exception& error)
			{
				feature->Enabled = false;
				std::cerr << "[Render] Feature '" << feature->GetName() << "' disabled: " << error.what() << '\n';
			}
		}
	}

	void FrameRenderer::Impl::RecordShadows(const SceneInputs& in, FeatureGeometry& geometry, ScenePasses& out)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& input = in.Input;
		const auto& camera = input.Camera;
		auto& stats = in.Stats;

		if (settings.Shadows && !input.ShadowCasters.empty())
		{
			if (!I.atlas || I.atlasSize != settings.Shadow.AtlasSize || I.atlasMinTile != settings.Shadow.MinTile)
			{
				I.atlas = std::make_unique<R::ShadowAtlasAllocator>(settings.Shadow.AtlasSize, settings.Shadow.MinTile);
				I.atlasSize = settings.Shadow.AtlasSize;
				I.atlasMinTile = settings.Shadow.MinTile;
			}

			R::Shadows::ShadowCamera shadowCamera;
			shadowCamera.View = camera.View;
			shadowCamera.VerticalFov = camera.VerticalFov;
			shadowCamera.Aspect = camera.Aspect;
			shadowCamera.Near = camera.Near;
			const auto plan = std::make_shared<R::ShadowPlan>(R::PlanShadows(settings.Shadow, shadowCamera, input.ShadowCasters, *I.atlas));

			if (!plan->Draws.empty())
			{
				R::ShadowFrame shadowFrame;
				shadowFrame.Scene = &in.Scene;
				shadowFrame.Geometry = &in.Geometry;
				shadowFrame.Visibility = I.shadowVisibility.get();
				shadowFrame.PageSlots = in.ShadowSlots;
				shadowFrame.Materials = &in.Materials;
				shadowFrame.Bindless = &I.bindless->GetTable();
				shadowFrame.Plan = plan.get();
				shadowFrame.ZeroUnusedCommands = R::NeedsZeroedCommands(I.drawPath);
				std::optional<R::GraphTexture> cachedAtlas;

				if (settings.ShadowCascadeCache && I.shadows->SupportsPersistentAtlas())
				{
					cachedAtlas = I.PlanShadowCache(graph, *plan, shadowFrame, camera, input.ShadowCasters, settings);
				}
				else
				{
					I.shadowCache.clear();
				}

				for (std::size_t v = 0; v < plan->Draws.size(); ++v)
				{
					RenderFeatureShadowView view;
					view.Index = static_cast<std::uint32_t>(v);
					std::copy(std::begin(plan->Draws[v].View.ViewProjection), std::end(plan->Draws[v].View.ViewProjection),
						view.ViewProjection.begin());
					view.Rendered = shadowFrame.Render.empty() || shadowFrame.Render[v] != 0;
					geometry.ShadowViews.push_back(view);
				}

				PrepareFeatureGeometry(in, geometry);
				shadowFrame.External = geometry.Draws;
				I.NoteStaticShadowDraws(geometry.Draws);

				out.Shadows = I.shadows->Record(graph, shadowFrame);

				if (cachedAtlas)
				{
					graph.Export(*cachedAtlas, ResourceState::ShaderRead);
				}

				if (shadowFrame.StaticAtlas)
				{
					graph.Export(*shadowFrame.StaticAtlas, ResourceState::ShaderRead);
				}

				stats.ShadowViews = plan->Stats.Views;
				stats.ShadowViewsRendered =
					static_cast<std::uint32_t>(std::count_if(geometry.ShadowViews.begin(), geometry.ShadowViews.end(),
						[](const auto& view)
						{
							return view.Rendered;
						}));
			}
		}
	}

	void FrameRenderer::Impl::RecordVisibility(const SceneInputs& in, ScenePasses& out)
	{
		const auto& settings = in.Settings;
		const auto& camera = in.Input.Camera;
		R::VisibilityFrameDesc visibilityFrame;
		visibilityFrame.View = R::BuildGpuViewRecord(in.ViewDesc);
		visibilityFrame.IndexPages = in.IndexPages;
		visibilityFrame.ReadStats = false;
		visibilityFrame.ZeroUnusedCommands = R::NeedsZeroedCommands(drawPath);
		out.Visible = visibility->Record(in.Graph, in.Scene, in.Geometry, visibilityFrame);

		auto& grid = out.Grid;
		grid.ViewportWidth = in.Width;
		grid.ViewportHeight = in.Height;
		grid.TileSize = settings.ClusterTileSize;
		grid.SliceCount = settings.ClusterSlices;
		grid.Near = camera.Near;
		grid.Far = std::max(settings.ClusterFar, camera.Near * 2.0f);
		grid.MaxLightsPerCluster = settings.MaxLightsPerCluster;
		// Per-cluster bitmasks over every local light (never truncated): the masks
		// address the frame's local lights, rounded up so the size changes rarely.
		grid.LightCapacity = std::max(32u, (in.Lights.LocalCount + 255u) / 256u * 256u);
		R::ClusterView clusterView;
		clusterView.View = camera.View;
		clusterView.Projection = camera.Projection;
		out.Clusters = clusters->Record(in.Graph, in.Lights, grid, clusterView);
	}

	std::optional<R::ForwardPlusGraphResources> FrameRenderer::Impl::RecordForward(
		const SceneInputs& in, const FeatureGeometry& geometry, ScenePasses& out)
	{
		auto& I = *this;
		const auto& settings = in.Settings;
		const auto& camera = in.Input.Camera;
		auto& forwardFrame = out.Forward;
		forwardFrame.Scene = &in.Scene;
		forwardFrame.Geometry = &in.Geometry;
		forwardFrame.Visibility = &*out.Visible;
		forwardFrame.PageSlots = in.ForwardSlots;
		forwardFrame.Materials = &in.Materials;
		forwardFrame.Bindless = &I.bindless->GetTable();
		forwardFrame.Lights = &in.Lights;
		forwardFrame.Clusters = &*out.Clusters;
		forwardFrame.Environment = in.Environment ? &*in.Environment : nullptr;
		forwardFrame.BrdfLut = in.Lut;
		forwardFrame.Shadows = out.Shadows ? &*out.Shadows : nullptr;
		forwardFrame.View.ViewProjection = in.ViewProjection;
		forwardFrame.View.PreviousViewProjection = I.previousViewProjection;
		forwardFrame.View.Jitter = in.Jitter;
		forwardFrame.View.CameraPosition = camera.Position;
		forwardFrame.View.CameraForward = camera.Forward;
		forwardFrame.View.Ambient = settings.Ambient;
		forwardFrame.View.MinimumLight = settings.MinimumLight;

		for (std::size_t i = 0; i < 4; ++i)
		{
			forwardFrame.View.Glint[i] = in.Glint.ColorA[i];
			forwardFrame.View.Glint[4 + i] = in.Glint.ColorB[i];
			forwardFrame.View.Glint[8 + i] = in.Glint.Motion[i];
			forwardFrame.View.Glint[12 + i] = in.Glint.Extra[i];
		}

		forwardFrame.View.GlintSource = in.Glint.Source;
		forwardFrame.View.EnvironmentIntensity = settings.EnvironmentIntensity;
		forwardFrame.View.EnvironmentRotation = settings.EnvironmentRotation;
		forwardFrame.View.DebugMode = settings.Debug;

		if (settings.Overlay.FieldOfView > 0.0f)
		{
			const float overlayFov = std::clamp(settings.Overlay.FieldOfView, 1.0f, 170.0f) * 0.0174532925f;
			forwardFrame.View.OverlayFovScale = std::tan(camera.VerticalFov * 0.5f) / std::tan(overlayFov * 0.5f);
		}

		forwardFrame.View.OverlayNear = std::max(settings.Overlay.Near, 1.0e-4f);
		forwardFrame.Transparent = settings.Transparent;
		forwardFrame.DeferLocalLights = settings.DeferredLocalLights;
		forwardFrame.External = geometry.Draws;

		if (std::any_of(in.Features.begin(), in.Features.end(), OccludesGeometry))
		{
			forwardFrame.BeforeOpaque = [this, &in](R::RenderGraph& g, std::optional<R::GraphTexture> depth)
			{
				const auto& viewProjection = in.ViewProjection;
				const auto width = in.Width;
				const auto height = in.Height;
				const auto& featureView = in.FeatureView;
				const auto& features = in.Features;
				RenderFeatureOcclusion occlusion;
				occlusion.ViewProjection = viewProjection;
				occlusion.Width = width;
				occlusion.Height = height;

				if (depth)
				{
					if (!hzbBuilder)
					{
						hzbProgram = shaders.LoadCompute("HzbReduce");
						R::HzbBuilderDesc hzbDesc;
						hzbDesc.ReducePipeline = hzbProgram.Pipeline.get();
						hzbDesc.Layout = hzbProgram.Layout.get();
						hzbDesc.Space = hzbProgram.Space;
						hzbDesc.DebugName = "Feature HZB";
						hzbBuilder = std::make_unique<R::HzbBuilder>(hzbDesc);
					}

					const auto hzb = hzbBuilder->Record(g, *depth);
					occlusion.Pyramid = hzb.Pyramid;
					occlusion.Width = hzb.Width;
					occlusion.Height = hzb.Height;
					occlusion.MipCount = hzb.MipCount;
				}
				else
				{
					S::TextureDesc standIn;
					standIn.Extent = { 1, 1, 1 };
					standIn.PixelFormat = S::Format::R32Float;
					standIn.Usage = S::TextureUsage::Sampled | S::TextureUsage::TransferDestination;
					standIn.DebugName = "No HZB";
					occlusion.Pyramid = g.CreateTexture(standIn);
					constexpr float nothing = 0.0f;
					R::AddTextureUpload(g, "No HZB", std::as_bytes(std::span(&nothing, 1)), occlusion.Pyramid, { 0, {}, {}, { 1, 1, 1 } });
				}

				auto services = FeatureServices();
				RenderFeatureContext context(g, RenderFeatureStage::BeforeTemporal, featureView, in.Settings, {}, {}, services);

				for (const auto& feature : features)
				{
					if (!OccludesGeometry(feature))
					{
						continue;
					}

					try
					{
						feature->CullGeometry(context, occlusion);
					}
					catch (const std::exception& error)
					{
						feature->Enabled = false;
						std::cerr << "[Render] Feature '" << feature->GetName() << "' disabled: " << error.what() << '\n';
					}
				}
			};
		}

		if (std::any_of(geometry.Draws.begin(), geometry.Draws.end(), CopiesScene))
		{
			forwardFrame.BeforeTransparent = [this, &in](R::RenderGraph& g, R::GraphTexture color, R::GraphTexture depth)
			{
				const auto width = in.Width;
				const auto height = in.Height;
				const auto& featureView = in.FeatureView;
				auto services = FeatureServices();
				RenderFeatureContext context(g, RenderFeatureStage::BeforeTemporal, featureView, in.Settings, color, depth, services);
				R::ExternalSceneCopy scene;
				scene.Color = context.CreateTexture(S::Format::RGBA16Float, width, height, "Scene colour copy");
				scene.Depth = context.CreateTexture(S::Format::R32Float, width, height, "Scene depth copy");
				context.Compute("SceneCopy")
					.Texture("Color", color)
					.Texture("Depth", depth)
					.Storage("ColorCopy", *scene.Color)
					.Storage("DepthCopy", *scene.Depth)
					.Dispatch(width, height);
				return scene;
			};
		}

		if (in.MsaaTargets)
		{
			// Multisampled: the opaque geometry into the multisampled targets, the resolve, then
			// the back depth and the transparent layers at one sample over the resolved result.
			auto opaqueFrame = forwardFrame;
			opaqueFrame.Transparent = false;
			opaqueFrame.DeferLocalLights = false;
			opaqueFrame.BeforeTransparent = {};
			I.msaaPasses.at(static_cast<std::uint32_t>(in.MsaaTargets->Samples)).Forward->Record(in.Graph, opaqueFrame, *in.MsaaTargets);
			I.RecordMsaaResolve(in.Graph, *in.MsaaTargets, in.Targets, in.Width, in.Height);
			auto layerFrame = forwardFrame;
			layerFrame.Opaque = false;
			layerFrame.BeforeOpaque = {};
			return forward->Record(in.Graph, layerFrame, in.Targets);
		}

		return forward->Record(in.Graph, forwardFrame, in.Targets);
	}

} // namespace Engine
