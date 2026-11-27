#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	namespace
	{
		// The atlas texels of a tile that the changed boxes cover through its view (row-major,
		// +Y-up NDC), padded by two texels; empty when none reaches the tile.
		std::optional<R::ShadowRect> DirtyShadowRect(const float* m, const R::ShadowTile& tile, std::span<const R::GpuSceneBox> boxes)
		{
			const float size = float(tile.Size);
			float x0 = size;
			float y0 = size;
			float x1 = 0.0f;
			float y1 = 0.0f;

			for (const auto& box : boxes)
			{
				float bx0 = std::numeric_limits<float>::max();
				float by0 = bx0;
				float bx1 = -bx0;
				float by1 = -bx0;

				for (int corner = 0; corner < 8; ++corner)
				{
					const float p[3] = { (corner & 1) ? box.Max[0] : box.Min[0], (corner & 2) ? box.Max[1] : box.Min[1],
						(corner & 4) ? box.Max[2] : box.Min[2] };
					const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];

					if (!(w > 1.0e-6f))
					{
						return R::ShadowRect{ tile.X, tile.Y, tile.Size, tile.Size }; // Behind a perspective eye: all of it.
					}

					const float u = ((m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3]) / w * 0.5f + 0.5f) * size;
					const float v = (0.5f - (m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7]) / w * 0.5f) * size;
					bx0 = std::min(bx0, u);
					bx1 = std::max(bx1, u);
					by0 = std::min(by0, v);
					by1 = std::max(by1, v);
				}

				if (bx1 < 0.0f || by1 < 0.0f || bx0 > size || by0 > size)
				{
					continue;
				}

				x0 = std::min(x0, bx0);
				y0 = std::min(y0, by0);
				x1 = std::max(x1, bx1);
				y1 = std::max(y1, by1);
			}

			if (x1 <= x0 || y1 <= y0)
			{
				return std::nullopt;
			}

			const auto left = std::uint32_t(std::clamp(std::floor(x0) - 2.0f, 0.0f, size));
			const auto top = std::uint32_t(std::clamp(std::floor(y0) - 2.0f, 0.0f, size));
			const auto right = std::uint32_t(std::clamp(std::ceil(x1) + 2.0f, 0.0f, size));
			const auto bottom = std::uint32_t(std::clamp(std::ceil(y1) + 2.0f, 0.0f, size));
			return R::ShadowRect{ tile.X + left, tile.Y + top, right - left, bottom - top };
		}
	} // namespace

	void FrameRenderer::Impl::EnsureShadowAtlases(std::uint32_t size, bool staticLayer)
	{
		if (!shadowAtlasTexture || shadowAtlasTexture->GetDesc().Extent.Width != size)
		{
			S::TextureDesc atlasDesc;
			atlasDesc.Extent = { size, size, 1 };
			atlasDesc.PixelFormat = R::ShadowRenderer::AtlasFormat;
			atlasDesc.Usage = S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled | S::TextureUsage::TransferSource |
				S::TextureUsage::TransferDestination;
			atlasDesc.DebugName = "Shadow atlas (cached)";
			shadowAtlasTexture = device.CreateTexture(atlasDesc);

			if (!shadowAtlasTexture)
			{
				throw std::runtime_error("FrameRenderer: cannot create the cached shadow atlas");
			}

			shadowAtlasWritten = false;
			shadowCache.clear();
		}

		if (!staticLayer)
		{
			shadowStaticTexture.reset();
			shadowStaticWritten = false;
		}
		else if (!shadowStaticTexture || shadowStaticTexture->GetDesc().Extent.Width != size)
		{
			S::TextureDesc staticDesc = shadowAtlasTexture->GetDesc();
			staticDesc.DebugName = "Shadow atlas (static layer)";
			shadowStaticTexture = device.CreateTexture(staticDesc);

			if (!shadowStaticTexture)
			{
				throw std::runtime_error("FrameRenderer: cannot create the static shadow atlas");
			}

			shadowStaticWritten = false;
		}
	}

	R::GraphTexture FrameRenderer::Impl::PlanShadowCache(R::RenderGraph& graph, R::ShadowPlan& plan, R::ShadowFrame& shadowFrame,
		const RenderCamera& camera, std::span<const R::ShadowCasterDesc> casters, const RenderSettings& settings)
	{
		const auto size = plan.AtlasSize;
		const bool staticLayer = settings.ShadowStaticLayer;
		const bool dynamicEveryFrame = staticLayer && settings.ShadowDynamicEveryFrame;

		EnsureShadowAtlases(size, staticLayer);

		// Static content changed (or the layer is new): every cascade takes a new view and
		// redraws its static tile.
		const std::uint64_t staticScene = scene->GetStaticGeneration();
		const bool staticStale = staticLayer && (!shadowStaticWritten || shadowStaticDirty || staticScene != shadowStaticScene);
		shadowStaticScene = staticScene;
		shadowStaticDirty = false;

		++shadowFrames;

		if (shadowFrames != lastShadowFrame + 1)
		{
			shadowCache.clear(); // Shadows were off for a while: nothing cached is current.
		}

		lastShadowFrame = shadowFrames;
		const auto dot3 = [](const std::array<float, 3>& a, const std::array<float, 3>& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		};
		shadowFrame.Render.assign(plan.Draws.size(), 1);
		// ShadowFrame::StaticRender: spot and point views are drawn whole every frame (2).
		std::vector<std::uint8_t> staticRender(plan.Draws.size(), 2);
		std::vector<R::ShadowRect> staticRects(plan.Draws.size());
		// Moving casters are known (no unlisted dynamic draws, not too many changes): a view
		// drawn last frame redraws only where they were and are.
		const bool changesKnown = staticLayer && !shadowDynamicExternal && !scene->AllDynamicChanged();
		const auto changes = scene->GetDynamicChanges();
		std::map<std::pair<std::uint32_t, std::uint32_t>, CachedShadowView> kept;

		for (std::size_t v = 0; v < plan.Draws.size(); ++v)
		{
			auto& draw = plan.Draws[v];

			if (draw.Kind != R::ShadowKind::Directional)
			{
				continue;
			}

			std::array<float, 3> lightDirection{};

			for (const auto& caster : casters)
			{
				if (caster.Slot == draw.Slot)
				{
					lightDirection = { caster.Light.Direction[0], caster.Light.Direction[1], caster.Light.Direction[2] };
				}
			}

			auto& record = plan.Records[draw.Slot];
			const auto key = std::make_pair(draw.Slot, draw.Index);
			const auto found = shadowCache.find(key);
			bool refresh = found == shadowCache.end() || !shadowAtlasWritten || staticStale;
			bool due = false;

			if (!refresh)
			{
				const auto& cached = found->second;
				const std::uint64_t period = draw.Index == 0 || dynamicEveryFrame ? 1u : draw.Index == 1 ? 2u : 4u;
				const std::array<float, 3> moved{ camera.Position[0] - cached.CameraPosition[0],
					camera.Position[1] - cached.CameraPosition[1], camera.Position[2] - cached.CameraPosition[2] };
				const float turn = std::acos(std::clamp(dot3(camera.Forward, cached.CameraForward), -1.0f, 1.0f));
				// The cascade's radius, from its texel size; the slice centre sits at most its
				// split distance away, so a turn shifts it by at most far * angle.
				const float radius = draw.View.TexelWorldSize * float(draw.Tile.Size) * 0.5f;
				const float shift = std::sqrt(dot3(moved, moved)) + record.CascadeFar[draw.Index] * turn;
				due = shadowFrames - cached.Frame >= period;
				refresh = cached.Tile.X != draw.Tile.X || cached.Tile.Y != draw.Tile.Y || cached.Tile.Size != draw.Tile.Size ||
					dot3(lightDirection, cached.LightDirection) < 0.99999f || shift > 0.05f * radius || (due && !staticLayer);
			}

			if (refresh)
			{
				CachedShadowView entry;
				entry.View = plan.Views[v];
				entry.CascadeFar = record.CascadeFar[draw.Index];
				entry.Frame = shadowFrames;
				entry.CameraPosition = camera.Position;
				entry.CameraForward = camera.Forward;
				entry.LightDirection = lightDirection;
				entry.Tile = draw.Tile;
				kept[key] = entry;
				staticRender[v] = 1;
			}
			else
			{
				// Kept view: a due cascade redraws only its dynamic casters over the static layer.
				staticRender[v] = 0;
				auto cached = found->second;
				plan.Views[v] = cached.View;
				draw.View = cached.View;
				record.CascadeFar[draw.Index] = cached.CascadeFar;
				shadowFrame.Render[v] = due ? 1 : 0;

				if (due)
				{
					if (changesKnown && cached.Frame + 1 == shadowFrames)
					{
						const auto dirty = DirtyShadowRect(draw.View.ViewProjection, draw.Tile, changes);
						shadowFrame.Render[v] = dirty ? 1 : 0;
						staticRects[v] = dirty.value_or(R::ShadowRect{});
					}

					cached.Frame = shadowFrames;
				}

				kept[key] = cached;
			}
		}

		shadowCache = std::move(kept);
		const auto atlas =
			graph.ImportTexture(*shadowAtlasTexture, shadowAtlasWritten ? S::ResourceState::ShaderRead : S::ResourceState::Undefined);

		if (!shadowAtlasWritten)
		{
			// First use: the atlas is cleared whole and every tile is drawn this frame.
			std::fill(shadowFrame.Render.begin(), shadowFrame.Render.end(), std::uint8_t(1));
			shadowFrame.AtlasHoldsDepth = false;
		}

		// An abandoned frame drew nothing: the next one starts both atlases over.
		graph.OnAbandoned(
			[this]
			{
				shadowAtlasWritten = false;
				shadowStaticWritten = false;
			});
		shadowAtlasWritten = true;
		shadowFrame.Atlas = atlas;

		if (staticLayer)
		{
			shadowFrame.StaticAtlas = graph.ImportTexture(
				*shadowStaticTexture, shadowStaticWritten ? S::ResourceState::ShaderRead : S::ResourceState::Undefined);
			shadowFrame.StaticAtlasHoldsDepth = shadowStaticWritten;
			shadowFrame.StaticRender = std::move(staticRender);
			shadowFrame.StaticRects = std::move(staticRects);

			shadowStaticWritten = true;
		}

		return atlas;
	}

	void FrameRenderer::Impl::NoteStaticShadowDraws(std::span<const R::ExternalGeometryDraws> draws)
	{
		std::uint64_t generation = 0;
		shadowDynamicExternal = false;

		for (const auto& draw : draws)
		{
			if (draw.StaticShadow)
			{
				generation = generation * 1000003u + draw.ShadowGeneration + 1u;
			}
			else if ((draw.Passes & R::ExternalGeometryPassBit(R::ExternalGeometryPass::Shadow)) != 0)
			{
				shadowDynamicExternal = true;
			}
		}

		shadowStaticDirty = shadowStaticDirty || generation != shadowStaticExternal;
		shadowStaticExternal = generation;
	}

	void FrameRenderer::Impl::RecordSky(R::RenderGraph& graph, const RenderSettings& settings, const R::ForwardPlusTargets& skyTargets,
		const std::array<float, 3>& rayRight, const std::array<float, 3>& rayUp, const std::array<float, 3>& rayForward,
		std::uint32_t skyWidth, std::uint32_t skyHeight, const char* passName)
	{
		auto& I = *this;
		SkyConstants sky{};
		const bool drawSky = settings.SkyBackground;
		const auto& s = settings.Sky;
		const auto sun = Normalized(s.SunDirection);

		for (int c = 0; c < 3; ++c)
		{
			sky.RayRight[c] = rayRight[c];
			sky.RayUp[c] = rayUp[c];
			sky.RayForward[c] = rayForward[c];
			sky.Zenith[c] = drawSky ? s.ZenithColor[c] : settings.ClearColor[c];
			sky.Horizon[c] = drawSky ? s.HorizonColor[c] : settings.ClearColor[c];
			sky.Ground[c] =
				drawSky ? (settings.SkyBackgroundGround ? (*settings.SkyBackgroundGround)[c] : s.GroundColor[c]) : settings.ClearColor[c];
			sky.SunDirection[c] = sun[c];
			sky.SunColor[c] = drawSky ? s.SunColor[c] : 0.0f;
		}

		sky.RayRight[3] = drawSky ? s.Intensity * settings.EnvironmentIntensity : 1.0f;
		sky.RayUp[3] = settings.EnvironmentRotation;
		sky.RayForward[3] = std::max(s.SunSharpness, 1.0f);
		sky.Ground[3] = s.GroundFalloff;
		const auto skySamples = graph.GetDesc(skyTargets.Color).Samples;
		auto* pipeline = skySamples == S::SampleCount::X1 ? I.skyPipeline.get()
														   : I.msaaPasses.at(static_cast<std::uint32_t>(skySamples)).Sky.get();
		// A cube map sky (FrameRenderer::SetSkyCubemap) replaces the procedural one.
		std::optional<R::GraphTexture> cube;
		auto* cubeLayout = I.skyCubeProgram.Layout.get();
		auto* cubeSampler = I.linearClamp.get();

		if (drawSky && !I.skyCubeFaces.empty())
		{
			if (auto* cubePipeline = I.SkyCubePipeline(skySamples))
			{
				cube = I.ImportSkyCube(graph);
				pipeline = cubePipeline;
				cubeLayout = I.skyCubeProgram.Layout.get();
				sky.RayRight[3] = std::max(settings.SkyCubemapIntensity, 0.0f);
			}
		}

		const auto set = skyTargets;
		graph.AddPass(
			passName, S::QueueType::Graphics,
			[&](R::RenderGraphBuilder& b)
			{
				if (cube)
				{
					b.Read(*cube, ResourceState::ShaderRead);
				}

				b.Write(set.Color, ResourceState::ColorAttachment);
				b.Write(set.Depth, ResourceState::DepthStencilWrite);
			},
			[set, sky, pipeline, skyWidth, skyHeight, cube, cubeLayout, cubeSampler](R::RenderCommandContext& c)
			{
				S::DescriptorTable* cubeTable = nullptr;

				if (cube)
				{
					auto table = c.CreateDescriptorTable({ cubeLayout, 0, 0, "Sky cube" });

					if (!table)
					{
						throw std::runtime_error("FrameRenderer: cannot create the sky cube descriptor table");
					}

					S::TextureViewDesc cubeView;
					cubeView.Dimension = S::TextureViewDimension::TextureCube;
					cubeView.PixelFormat = S::Format::RGBA8UnormSrgb;
					cubeView.ArrayLayerCount = 6;
					std::array<S::DescriptorWrite, 2> writes{};
					writes[0].Binding = 0;
					writes[0].TextureResource = &c.CreateView(*cube, cubeView);
					writes[1].Binding = 1;
					writes[1].SamplerResource = cubeSampler;
					table->Write(writes);
					cubeTable = &static_cast<S::DescriptorTable&>(c.Retain(std::move(table)));
				}

				std::array<S::RenderingAttachmentDesc, 1> colors{};
				const std::array<R::GraphTexture, 1> textures{ set.Color };

				for (std::size_t i = 0; i < colors.size(); ++i)
				{
					colors[i].View = &c.CreateView(textures[i]);
					colors[i].Load = S::LoadOp::Clear;
					colors[i].Clear.Value = { 0.0f, 0.0f, 0.0f, 0.0f };
				}

				S::TextureViewDesc depthView;
				depthView.PixelFormat = R::CanonicalDepthFormat;
				const S::DepthStencilAttachmentDesc depth{ &c.CreateView(set.Depth, depthView), S::LoadOp::Clear, S::StoreOp::Store,
					R::DepthClearValue(R::CanonicalDepthConvention), 0 };
				auto& list = c.Commands();
				list.BeginRendering({ colors, &depth, { skyWidth, skyHeight } });
				list.BindGraphicsPipeline(*pipeline);

				if (cubeTable)
				{
					list.BindDescriptorTable(0, *cubeTable);
				}

				list.SetViewport({ 0, 0, float(skyWidth), float(skyHeight) });
				list.SetScissor({ 0, 0, skyWidth, skyHeight });
				list.PushConstants(S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&sky, 1)));
				list.Draw(3);
				list.EndRendering();
			});
	}

	void FrameRenderer::Impl::RecordEnvironment(const EnvironmentInputs& in, const EnvironmentOutputs& out)
	{
		auto& I = *this;
		auto& graph = in.Graph;
		const auto& settings = in.Settings;
		const auto& featureView = in.FeatureView;
		const auto& features = in.Features;
		const bool draw3D = in.Draw3D;
		auto& environment = out.Environment;
		auto& lut = out.Lut;
		auto& environmentRebuilt = out.Rebuilt;
		auto& environmentWithFeatures = out.WithFeatures;

		if (!I.brdfLut)
		{
			I.brdfLut = device.CreateTexture(R::EnvironmentBuilder::BrdfLutDesc(Impl::LutSize));

			if (!I.brdfLut)
			{
				throw std::runtime_error("FrameRenderer: cannot create the BRDF LUT");
			}
		}

		if (!I.lutBuilt)
		{
			const auto target = graph.ImportTexture(*I.brdfLut, ResourceState::Undefined);
			lut = I.environmentBuilder->RecordBrdfLut(graph, Impl::LutSize, Impl::LutSamples, target);
		}
		else
		{
			lut = graph.ImportTexture(*I.brdfLut, ResourceState::ShaderRead);
		}

		graph.Export(*lut, ResourceState::ShaderRead);

		if (settings.Environment)
		{
			I.EnsureEnvironmentTargets(settings.EnvironmentResolution);
			// Features that draw into the sky (clouds) also draw into the environment; it is
			// then re-recorded every EnvironmentRefreshSeconds from the camera, because they move.
			std::vector<RenderFeature*> contributors;

			if (draw3D)
			{
				for (const auto& feature : features)
				{
					if (feature && feature->Enabled && feature->ContributesToEnvironment() &&
						!I.environmentFeatureFailures.contains(feature.get()))
					{
						contributors.push_back(feature.get());
					}
				}
			}

			environmentWithFeatures = !contributors.empty();
			const bool refresh = environmentWithFeatures && settings.EnvironmentUpdates &&
				I.featureTime - I.environmentBuiltTime >= static_cast<double>(settings.EnvironmentRefreshSeconds);
			const bool rebuild = !I.builtSky || !SameSky(*I.builtSky, settings.Sky) || !I.environmentValid ||
				environmentWithFeatures != I.environmentHadFeatures || refresh;
			environmentRebuilt = rebuild;
			R::EnvironmentTargets targets;
			targets.Prefiltered = graph.ImportTexture(*I.prefiltered, rebuild ? ResourceState::Undefined : ResourceState::ShaderRead);
			targets.Irradiance = graph.ImportBuffer(*I.irradiance, rebuild ? ResourceState::Undefined : ResourceState::ShaderRead);

			if (rebuild)
			{
				std::vector<R::GraphTexture> overlays;

				if (environmentWithFeatures)
				{
					RenderFeatureContext context(
						graph, RenderFeatureStage::BeforeTemporal, featureView, settings, {}, {}, I.FeatureServices());

					for (auto* feature : contributors)
					{
						// A failing contribution is dropped (with one log line); the feature itself keeps running.
						try
						{
							if (const auto overlay = feature->RecordEnvironment(context, I.environmentMap.SourceSize))
							{
								overlays.push_back(*overlay);
							}
						}
						catch (const std::exception& error)
						{
							I.environmentFeatureFailures.insert(feature);
							std::cerr << "[Render] Feature '" << feature->GetName() << "' dropped from the environment: " << error.what()
									  << '\n';
						}
					}
				}

				environment = I.environmentBuilder->Record(
					graph, settings.Sky, I.environmentMap, targets, overlays, settings.EnvironmentFeatureAmbient);
			}
			else
			{
				R::EnvironmentGraphResources existing;
				existing.Prefiltered = *targets.Prefiltered;
				existing.Irradiance = *targets.Irradiance;
				existing.SourceSize = I.environmentMap.SourceSize;
				existing.PrefilteredSize = I.environmentMap.PrefilteredSize;
				existing.PrefilteredMipCount = I.environmentMap.PrefilteredMipCount;
				environment = existing;
			}

			graph.Export(*targets.Prefiltered, ResourceState::ShaderRead);
			graph.Export(*targets.Irradiance, ResourceState::ShaderRead);
		}
	}

	RenderFeatureView FrameRenderer::Impl::BuildFeatureView(const RenderFrameInput& input, const RenderSettings& settings,
		const std::array<float, 16>& viewProjection, const std::array<float, 2>& jitter, bool cut, bool temporal, std::uint32_t width,
		std::uint32_t height)
	{
		const auto& camera = input.Camera;
		RenderFeatureView featureView;
		featureView.View = camera.View;
		featureView.Projection = camera.Projection;
		featureView.ViewProjection = viewProjection;
		featureView.Position = camera.Position;
		featureView.Forward = camera.Forward;
		featureView.Right = camera.Right;
		featureView.Up = camera.Up;
		featureView.TanHalfFovY = std::tan(camera.VerticalFov * 0.5f);
		featureView.TanHalfFovX = featureView.TanHalfFovY * camera.Aspect;
		featureView.Width = width;
		featureView.Height = height;
		{
			const auto& sun = settings.Sky.SunDirection;
			const float length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);

			for (int c = 0; c < 3; ++c)
			{
				featureView.SunDirection[c] = length > 0.0f ? sun[c] / length : (c == 1 ? 1.0f : 0.0f);
				featureView.SunColor[c] = settings.Sky.SunColor[c] * settings.Sky.Intensity;
			}
		}
		const float frameSeconds = std::clamp(std::isfinite(input.DeltaTime) ? input.DeltaTime : 0.0f, 0.0f, 1.0f);
		featureTime += frameSeconds;
		featureView.Time = static_cast<float>(featureTime);
		featureView.DeltaTime = frameSeconds;
		featureView.Frame = static_cast<std::uint32_t>(frameIndex);
		featureView.PreviousViewProjection = previousViewProjection.value_or(viewProjection);
		featureView.Jitter = jitter;
		featureView.Cut = cut;
		featureView.Temporal = temporal;
		return featureView;
	}

	FrameRenderer::Impl::FrameImports FrameRenderer::Impl::ImportFrame(R::RenderGraph& graph, FramePending& pending)
	{
		if (!whiteUploaded)
		{
			const auto white = graph.ImportTexture(*whiteTexture, S::ResourceState::Undefined);
			static const std::array<std::uint8_t, 4> texel{ 255, 255, 255, 255 };
			R::AddTextureUpload(graph, "Fallback texture", std::as_bytes(std::span(texel)), white, { 0, {}, {}, { 1, 1, 1 } });
			graph.Export(white, S::ResourceState::ShaderRead);
		}

		FrameImports imports;
		imports.Residency = residency->Import(graph);
		pending.Residency = true;

		if (skinning->GetStats().Instances > 0 || skinning->GetStats().PendingMeshes > 0)
		{
			skinning->Record(graph, imports.Residency.Geometry);
			pending.Skinning = true;
		}

		imports.Materials = materialTable->Import(graph);
		pending.Materials = true;
		imports.Scene = scene->Import(graph);
		pending.Scene = true;
		imports.Lights = lights->Import(graph);
		pending.Lights = true;
		return imports;
	}

} // namespace Engine
