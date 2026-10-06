#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentBindings.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRenderer.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessor.h"
#include "Engine/Systems/Renderer/Reflections/PlanarReflectionRenderer.h"
#include "Engine/Systems/Renderer/Reflections/PlanarReflections.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeRenderer.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbes.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceEffects.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/Shadows/ShadowAtlasAllocator.h"
#include "Engine/Systems/Renderer/Shadows/ShadowBindings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRenderer.h"
#include "Engine/Systems/Renderer/Skinning/SkinningSystem.h"
#include "Engine/Systems/Renderer/Temporal/TemporalAntiAliasing.h"
#include "Engine/Systems/Renderer/UiRendering/UiAtlasTextures.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace Engine
{

	namespace S = Swim::Rhi;
	namespace R = Swim::Render;
	static_assert(sizeof(R::GpuReflectionProbeRecord) == R::ScreenSpaceProbeRecordBytes && R::MaxReflectionProbes == R::ScreenSpaceMaxProbes);

	namespace
	{

		using S::ResourceState;

		// Presentation push constants (Present.slang).
		struct PresentConstants
		{
			std::uint32_t DecodeSrgb = 0;
			std::uint32_t Reserved[3] = {};
		};

		// SkyBackground.slang's push constants (128 bytes).
		struct SkyConstants
		{
			float RayRight[4];
			float RayUp[4];
			float RayForward[4];
			float Zenith[4];
			float Horizon[4];
			float Ground[4];
			float SunDirection[4];
			float SunColor[4];
		};

		static_assert(sizeof(SkyConstants) == 128);

		bool IsSrgbFormat(S::Format format)
		{
			return format == S::Format::RGBA8UnormSrgb || format == S::Format::BGRA8UnormSrgb;
		}

		std::array<float, 3> Normalized(std::array<float, 3> v)
		{
			const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

			if (length <= 1e-12f || !std::isfinite(length))
			{
				return { 0, 0, -1 };
			}

			return { v[0] / length, v[1] / length, v[2] / length };
		}

		bool SameSky(const R::Environment::ProceduralSky& a, const R::Environment::ProceduralSky& b)
		{
			return a.ZenithColor == b.ZenithColor && a.HorizonColor == b.HorizonColor && a.GroundColor == b.GroundColor &&
				a.GroundFalloff == b.GroundFalloff &&
				a.SunDirection == b.SunDirection && a.SunColor == b.SunColor && a.SunSharpness == b.SunSharpness &&
				a.Intensity == b.Intensity;
		}

		std::uint32_t PowerOfTwoFloor(std::uint32_t value)
		{
			std::uint32_t result = 1;

			while (result * 2 <= value)
			{
				result *= 2;
			}

			return result;
		}

	} // namespace

	void RenderSettings::Sanitize()
	{
		const auto clampFinite = [](float& value, float low, float high, float fallback)
		{
			value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		};
		clampFinite(Sky.GroundFalloff, 1.0f, 256.0f, 2.0f);

		if (SkyBackgroundGround)
		{
			for (auto& channel : *SkyBackgroundGround)
			{
				clampFinite(channel, 0.0f, 1000.0f, 0.0f);
			}
		}

		for (auto& a : Ambient)
		{
			clampFinite(a, 0.0f, 100.0f, 0.0f);
		}

		clampFinite(EnvironmentIntensity, 0.0f, 100.0f, 1.0f);
		clampFinite(EnvironmentRotation, -100.0f, 100.0f, 0.0f);
		clampFinite(EnvironmentRefreshSeconds, 0.0f, 3600.0f, 0.5f);
		EnvironmentResolution = std::clamp(PowerOfTwoFloor(std::max(EnvironmentResolution, 16u)), 16u, 1024u);
		ReflectionProbes.Resolution = std::clamp(PowerOfTwoFloor(std::max(ReflectionProbes.Resolution, 16u)), 16u, 512u);
		ReflectionProbes.MaxProbes = std::clamp(ReflectionProbes.MaxProbes, 1u, Swim::Render::MaxReflectionProbes);
		ReflectionProbes.FacesPerFrame = std::min(ReflectionProbes.FacesPerFrame, 12u);
		ReflectionProbes.PrefilterSamples = std::clamp(ReflectionProbes.PrefilterSamples, 1u, 256u);
		ReflectionProbes.FiltersPerFrame = std::clamp(ReflectionProbes.FiltersPerFrame, 1u, Swim::Render::MaxReflectionProbes);
		clampFinite(ReflectionProbes.MoveThreshold, 0.0f, 100.0f, 0.05f);
		clampFinite(ReflectionProbes.MoverRange, 0.0f, 1000.0f, 15.0f);
		auto& planar = PlanarReflections;
		planar.MaxPlanes = std::clamp(planar.MaxPlanes, 1u, Swim::Render::MaxPlanarReflections);
		planar.AtlasResolution = std::clamp(PowerOfTwoFloor(std::max(planar.AtlasResolution, 64u)), 64u, 2048u);
		planar.MinResolution = std::clamp(planar.MinResolution, 8u, planar.AtlasResolution);
		planar.CapturesPerFrame = std::min(planar.CapturesPerFrame, Swim::Render::MaxPlanarReflections);
		clampFinite(planar.ResolutionScale, 0.05f, 4.0f, 0.5f);
		clampFinite(planar.MinScreenFraction, 0.0f, 1.0f, 0.04f);
		clampFinite(planar.SsrHandoff, -1.0f, 1.0f, 0.85f);
		clampFinite(planar.PlaneAngleTolerance, 0.0f, 0.5f, 0.035f);
		clampFinite(planar.PlaneDistanceTolerance, 0.0f, 10.0f, 0.05f);
		clampFinite(planar.MotionTolerance, 0.0f, 1000.0f, 3.0f);
		clampFinite(planar.MaxAgeSeconds, 0.0f, 3600.0f, 0.25f);
		clampFinite(planar.SphereMinCosine, 0.5f, 0.999f, 0.9f);
		clampFinite(planar.RoughnessFadeStart, 0.0f, 1.0f, 0.15f);
		clampFinite(planar.RoughnessFadeEnd, planar.RoughnessFadeStart, 1.0f, 0.35f);
		clampFinite(planar.PortalMargin, 0.0f, 2.0f, 0.1f);
		clampFinite(planar.CullDistance, 1.0f, 100000.0f, 150.0f);
		ClusterTileSize = std::clamp(ClusterTileSize, 8u, 256u);
		ClusterSlices = std::clamp(ClusterSlices, 1u, 64u);
		clampFinite(ClusterFar, 1.0f, 100000.0f, 200.0f);
		MaxLightsPerCluster = std::clamp(MaxLightsPerCluster, 1u, 1024u);
		Shadow.AtlasSize = std::clamp(PowerOfTwoFloor(std::max(Shadow.AtlasSize, 256u)), 256u, 8192u);
		Shadow.MinTile = std::clamp(PowerOfTwoFloor(std::max(Shadow.MinTile, 16u)), 16u, Shadow.AtlasSize);
		Shadow.CascadeResolution = std::clamp(PowerOfTwoFloor(std::max(Shadow.CascadeResolution, 64u)), 64u, Shadow.AtlasSize);
		Shadow.SpotResolution = std::clamp(PowerOfTwoFloor(std::max(Shadow.SpotResolution, 64u)), 64u, Shadow.AtlasSize);
		Shadow.PointResolution = std::clamp(PowerOfTwoFloor(std::max(Shadow.PointResolution, 64u)), 64u, Shadow.AtlasSize);
		Shadow.Cascades.Count = std::clamp(Shadow.Cascades.Count, 1u, 4u);
		clampFinite(Shadow.Cascades.MaxDistance, 1.0f, 10000.0f, 70.0f);
		clampFinite(Shadow.Cascades.SplitLambda, 0.0f, 1.0f, 0.8f);
		clampFinite(Shadow.CascadeBlend, 0.0f, 0.5f, 0.2f);
		Temporal.JitterPhases = std::min(Temporal.JitterPhases, Swim::Render::MaxJitterPhases);
		clampFinite(Temporal.Feedback, 0.01f, 1.0f, 0.1f);
		clampFinite(Temporal.ClipGamma, 0.25f, 8.0f, 1.25f);
		clampFinite(Post.Exposure.Compensation, -10.0f, 10.0f, 0.0f);
		clampFinite(Post.Bloom.Intensity, 0.0f, 1.0f, 0.04f);
		clampFinite(Post.Bloom.Threshold, 0.0f, 100.0f, 1.0f);
		clampFinite(Post.Grading.Saturation, 0.0f, 4.0f, 1.0f);
		clampFinite(Post.Grading.Contrast, 0.05f, 4.0f, 1.0f);
		clampFinite(Post.Grading.Temperature, -100.0f, 100.0f, 0.0f);
		clampFinite(ScreenSpace.AmbientOcclusion.Radius, 0.01f, 10.0f, 0.5f);
		clampFinite(ScreenSpace.AmbientOcclusion.Power, 0.1f, 8.0f, 1.0f);
		clampFinite(ScreenSpace.Fog.Density, 0.0f, 10.0f, 0.02f);
		clampFinite(ScreenSpace.Fog.HeightFalloff, 0.0f, 10.0f, 0.1f);
	}

	struct FrameRenderer::Impl
	{
		Impl(RenderDevice& renderDevice, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io, Swim::Jobs::JobSystem* jobs,
			const FrameRendererDesc& desc);

		~Impl();

		S::Device& device;
		RenderDevice& renderDevice;
		ShaderLibrary shaders;
		R::VisibilityDrawPath drawPath;
		FrameRendererDesc desc;

		// Programs.
		RuntimeComputeProgram visibilityProgram, clusterCull, clusterBounds, clusterAssign, clusterScan, sortProgram;
		RuntimeComputeProgram environmentSky, environmentDownsample, environmentPrefilter, environmentIrradiance, environmentLut;
		RuntimeComputeProgram environmentOverlay; // Optional: feature overlays (clouds) in the environment.
		RuntimeComputeProgram probeResolve, probePrefilter; // Optional: reflection probes.
		RuntimeComputeProgram planarResolve;				// Optional: planar reflections.
		RuntimeComputeProgram postHistogram, postExposure, postBloomDown, postBloomUp, postComposite, postCompositeHdr;
		RuntimeComputeProgram temporalResolve, ssAo, ssBlur, ssComposite, ssReflection, ssReflectionTemporal;
		RuntimeComputeProgram particleSimulate, particleEmit, particleCompact, particleFinalize, skinningProgram;
		RuntimeGraphicsProgram forwardBackDepth; // Optional (screen-space reflection thickness).
		std::unique_ptr<S::GraphicsPipeline> forwardBackDepthPipeline;
		// Optional: deferred local lights (RenderSettings::DeferredLocalLights).
		RuntimeGraphicsProgram forwardDeferred;
		std::unique_ptr<S::GraphicsPipeline> forwardDeferredPipeline;
		RuntimeComputeProgram forwardLocalLights;
		RuntimeGraphicsProgram forwardOpaque, forwardTransparent, forwardDepth, forwardPrepassed, shadowDepth, shadowMasked, particleRender,
			uiQuad, skyBackground, present;
		std::unique_ptr<S::GraphicsPipeline> forwardOpaquePipeline, forwardTransparentPipeline, forwardDepthPipeline,
			forwardPrepassedPipeline, shadowDepthPipeline, shadowMaskedPipeline;
		std::unique_ptr<S::GraphicsPipeline> particleAdditive, particleAlpha, uiPipeline, uiDepthPipeline, skyPipeline;
		std::map<S::Format, std::unique_ptr<S::GraphicsPipeline>> presentPipelines;

		// Shared resources.
		std::unique_ptr<S::Texture> whiteTexture;
		std::unique_ptr<S::TextureView> whiteView;
		std::unique_ptr<S::Sampler> linearRepeat, linearClamp, presentSampler;
		std::unique_ptr<R::BindlessResourceTable> bindless;
		std::uint32_t materialSampler = 0;
		bool whiteUploaded = false;

		// Residency and scene.
		std::unique_ptr<R::GeometryHeap> geometry;
		std::unique_ptr<R::TextureResidency> textures;
		std::unique_ptr<R::AssetResidencyService> residency;
		std::shared_ptr<const R::MaterialTemplate> materialTemplate;
		std::unique_ptr<R::GpuMaterialTable> materialTable;
		std::unique_ptr<MaterialLibrary> materials;
		std::unique_ptr<MeshLibrary> meshes;
		std::unique_ptr<R::GpuScene> scene;
		std::unique_ptr<R::GpuLightBuffer> lights;
		std::unique_ptr<R::ParticleSystem> particles;
		std::unique_ptr<R::SkinningSystem> skinning;

		// Frame subsystems.
		std::unique_ptr<R::ForwardPlusRenderer> forward;
		std::unique_ptr<R::ShadowRenderer> shadows;
		std::unique_ptr<R::ClusteredLightAssigner> clusters;
		std::unique_ptr<R::EnvironmentBuilder> environmentBuilder;
		std::unique_ptr<R::ReflectionProbeRenderer> probeRenderer; // Null when its programs are missing.
		R::ReflectionProbes::Scheduler probeScheduler;
		// Probe slots whose faces changed and still need prefiltering (FiltersPerFrame a
		// frame), and the probe key each slot was last filtered for (a slot used before its
		// first filter for its current probe would show another probe's cube).
		std::vector<std::uint32_t> pendingFilters;
		std::vector<std::uint64_t> filteredKey;
		std::unique_ptr<R::PlanarReflectionRenderer> planarRenderer; // Null when its program is missing.
		R::PlanarReflections::Planner planarPlanner;
		std::unique_ptr<R::ScreenSpaceEffects> screenSpace;
		std::unique_ptr<R::TemporalAntiAliasing> temporal;
		std::unique_ptr<R::PostProcessor> post;
		std::unique_ptr<R::UiRenderer> ui;
		std::unique_ptr<R::UiAtlasTextures> atlasTextures;
		const Swim::Text::GlyphAtlas* attachedAtlas = nullptr;

		// Visibility instances (rebuilt when the page-slot count changes).
		std::unique_ptr<R::GpuVisibility> visibility;
		std::unique_ptr<R::GpuVisibility> shadowVisibility;
		std::unique_ptr<R::GpuVisibility> probeVisibility; // Reflection probe capture views (own LOD history).
		std::unique_ptr<R::GpuVisibility> planarVisibility; // Planar reflection captures (own LOD history).
		std::uint32_t visibilitySlots = 0;
		std::map<std::uint32_t, R::StandardPbr::Parameters> routes; // Material set -> parameters.

		// Shadows.
		std::unique_ptr<R::ShadowAtlasAllocator> atlas;
		std::uint32_t atlasSize = 0;
		std::uint32_t atlasMinTile = 0;
		// The cascade cache (RenderSettings::ShadowCascadeCache): a persistent atlas and, per
		// (slot, cascade), the view last drawn into its tile.
		RuntimeGraphicsProgram shadowClear;
		std::unique_ptr<S::GraphicsPipeline> shadowClearPipeline;
		std::unique_ptr<S::Texture> shadowAtlasTexture;
		bool shadowAtlasWritten = false;
		struct CachedShadowView
		{
			R::GpuShadowView View;
			float CascadeFar = 0.0f;
			std::uint64_t Frame = 0;
			std::array<float, 3> CameraPosition{};
			std::array<float, 3> CameraForward{};
			std::array<float, 3> LightDirection{};
			R::ShadowTile Tile;
		};
		std::map<std::pair<std::uint32_t, std::uint32_t>, CachedShadowView> shadowCache;
		// The temporal reflection filter's history (ReflectionSettings::Temporal): written and
		// read in turn; invalid after a cut, a resize or a frame without the filter.
		std::array<std::unique_ptr<S::Texture>, 2> reflectionHistory;
		std::uint32_t reflectionHistoryLatest = 0;
		bool reflectionHistoryValid = false;
		std::uint64_t shadowFrames = 0;
		std::uint64_t lastShadowFrame = 0;

		// The cascade cache: cascade 0 is drawn every frame, cascade 1 every second frame and
		// the others every fourth (staggered). A cascade is redrawn sooner when its tile moved,
		// the light turned, or the camera moved or turned enough to shift the cascade's slice by
		// a few percent of its radius. A cascade kept this frame keeps the view (and split
		// distance) its depth was drawn with, so lookups stay consistent; moving casters in far
		// cascades lag by at most three frames. Spot and point views are drawn every frame.
		// Returns the imported persistent atlas and fills shadowFrame's Atlas and Render.
		R::GraphTexture PlanShadowCache(R::RenderGraph& graph, R::ShadowPlan& plan, R::ShadowFrame& shadowFrame, const RenderCamera& camera,
			std::span<const R::ShadowCasterDesc> casters)
		{
			const auto size = plan.AtlasSize;

			if (!shadowAtlasTexture || shadowAtlasTexture->GetDesc().Extent.Width != size)
			{
				S::TextureDesc atlasDesc;
				atlasDesc.Extent = { size, size, 1 };
				atlasDesc.PixelFormat = R::ShadowRenderer::AtlasFormat;
				atlasDesc.Usage = S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled | S::TextureUsage::TransferSource;
				atlasDesc.DebugName = "Shadow atlas (cached)";
				shadowAtlasTexture = device.CreateTexture(atlasDesc);

				if (!shadowAtlasTexture)
				{
					throw std::runtime_error("FrameRenderer: cannot create the cached shadow atlas");
				}

				shadowAtlasWritten = false;
				shadowCache.clear();
			}

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
				bool refresh = found == shadowCache.end() || !shadowAtlasWritten;

				if (!refresh)
				{
					const auto& cached = found->second;
					const std::uint64_t period = draw.Index == 0 ? 1u : draw.Index == 1 ? 2u : 4u;
					const std::array<float, 3> moved{ camera.Position[0] - cached.CameraPosition[0],
						camera.Position[1] - cached.CameraPosition[1], camera.Position[2] - cached.CameraPosition[2] };
					const float turn = std::acos(std::clamp(dot3(camera.Forward, cached.CameraForward), -1.0f, 1.0f));
					// The cascade's radius, from its texel size; the slice centre sits at most its
					// split distance away, so a turn shifts it by at most far * angle.
					const float radius = draw.View.TexelWorldSize * float(draw.Tile.Size) * 0.5f;
					const float shift = std::sqrt(dot3(moved, moved)) + record.CascadeFar[draw.Index] * turn;
					refresh = shadowFrames - cached.Frame >= period || cached.Tile.X != draw.Tile.X || cached.Tile.Y != draw.Tile.Y ||
						cached.Tile.Size != draw.Tile.Size || dot3(lightDirection, cached.LightDirection) < 0.99999f ||
						shift > 0.05f * radius;
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
				}
				else
				{
					const auto& cached = found->second;
					plan.Views[v] = cached.View;
					draw.View = cached.View;
					record.CascadeFar[draw.Index] = cached.CascadeFar;
					shadowFrame.Render[v] = 0;
					kept[key] = cached;
				}
			}

			shadowCache = std::move(kept);
			const auto atlas = graph.ImportTexture(*shadowAtlasTexture, shadowAtlasWritten ? S::ResourceState::ShaderRead : S::ResourceState::Undefined);

			if (!shadowAtlasWritten)
			{
				// First use: the atlas is cleared whole and every tile is drawn this frame.
				std::fill(shadowFrame.Render.begin(), shadowFrame.Render.end(), std::uint8_t(1));
				shadowFrame.AtlasHoldsDepth = false;
			}

			shadowAtlasWritten = true;
			shadowFrame.Atlas = atlas;
			return atlas;
		}

		// Environment (persistent, rebuilt when the sky changes).
		std::unique_ptr<S::Texture> prefiltered;
		std::unique_ptr<S::Buffer> irradiance;
		std::unique_ptr<S::Texture> brdfLut;
		R::EnvironmentMapDesc environmentMap;
		std::optional<R::Environment::ProceduralSky> builtSky;
		bool lutBuilt = false;
		bool environmentValid = false;
		bool environmentHadFeatures = false; // The last build included feature overlays.
		double environmentBuiltTime = -1.0e9; // featureTime of the last build.
		std::unordered_set<const RenderFeature*> environmentFeatureFailures;
		static constexpr std::uint32_t LutSize = 128;
		static constexpr std::uint32_t LutSamples = 256;

		// History.
		std::optional<std::array<float, 16>> previousViewProjection;
		std::uint64_t frameIndex = 0;
		double featureTime = 0.0;
		std::unordered_map<std::string, std::unique_ptr<RuntimeComputeProgram>> featurePrograms; // Loaded on first use.

		void Route(std::uint32_t set, const R::StandardPbr::Parameters& parameters);

		void EnsureVisibility(std::uint32_t slots);

		RuntimeGraphicsProgram LoadDrawProgram(std::string_view name, const S::DescriptorSchemaDesc* bindlessSpace);

		S::GraphicsPipeline& GetPresentPipeline(S::Format format);

		void EnsureEnvironmentTargets(std::uint32_t resolution);
	};

	FrameRenderer::Impl::Impl(RenderDevice& renderDeviceValue, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io,
		Swim::Jobs::JobSystem* jobs, const FrameRendererDesc& descValue)
		: device(renderDeviceValue.GetDevice()), renderDevice(renderDeviceValue),
		  shaders(renderDeviceValue.GetDevice(), descValue.ShaderRoot),
		  drawPath(R::SelectVisibilityDrawPath(renderDeviceValue.GetCapabilities())), desc(descValue)
	{
		// SwiftShader advertises drawIndirectCount but implements vkCmdDrawIndexedIndirectCount
		// as an unsupported stub (nothing is drawn): use the zero-filled fallback there, and
		// wherever SWIM_FORCE_INDIRECT_FALLBACK=1 asks for it.
		constexpr std::uint32_t SwiftShaderVendor = 0x1AE0u;
		const char* forced = std::getenv("SWIM_FORCE_INDIRECT_FALLBACK");

		if (desc.ForceIndirectFallback || renderDeviceValue.GetAdapterInfo().VendorId == SwiftShaderVendor ||
			(forced && std::string_view(forced) == "1"))
		{
			drawPath = R::VisibilityDrawPath::ZeroFilledIndirect;
		}

		for (const auto name : ShaderLibrary::RequiredPrograms())
		{
			if (!shaders.Contains(name))
			{
				throw std::runtime_error(
					"FrameRenderer: shader program '" + std::string(name) + "' is missing from " + shaders.GetRoot().string());
			}
		}

		// --- Programs --------------------------------------------------------------
		visibilityProgram = shaders.LoadCompute("GpuVisibility");
		clusterCull = shaders.LoadCompute("ClusterLightCull");
		clusterBounds = shaders.LoadCompute("ClusterBounds");
		clusterAssign = shaders.LoadCompute("ClusterAssign");
		clusterScan = shaders.LoadCompute("ClusterScan");
		sortProgram = shaders.LoadCompute("ForwardTransparentSort");
		environmentSky = shaders.LoadCompute("EnvironmentSky");
		environmentDownsample = shaders.LoadCompute("EnvironmentDownsample");
		environmentPrefilter = shaders.LoadCompute("EnvironmentPrefilter");
		environmentIrradiance = shaders.LoadCompute("EnvironmentIrradiance");
		environmentLut = shaders.LoadCompute("EnvironmentBrdfLut");
		postHistogram = shaders.LoadCompute("PostHistogram");
		postExposure = shaders.LoadCompute("PostExposure");
		postBloomDown = shaders.LoadCompute("PostBloomDownsample");
		postBloomUp = shaders.LoadCompute("PostBloomUpsample");
		postComposite = shaders.LoadCompute("PostComposite");
		postCompositeHdr = shaders.LoadCompute("PostCompositeHdr");
		temporalResolve = shaders.LoadCompute("TemporalResolve");
		ssAo = shaders.LoadCompute("ScreenSpaceAo");
		ssBlur = shaders.LoadCompute("ScreenSpaceBlur");
		ssComposite = shaders.LoadCompute("ScreenSpaceComposite");
		ssReflection = shaders.LoadCompute("ScreenSpaceReflection");

		if (shaders.Contains("ScreenSpaceReflectionTemporal"))
		{
			ssReflectionTemporal = shaders.LoadCompute("ScreenSpaceReflectionTemporal");
		}

		particleSimulate = shaders.LoadCompute("ParticleSimulate");
		particleEmit = shaders.LoadCompute("ParticleEmit");
		particleCompact = shaders.LoadCompute("ParticleCompact");
		particleFinalize = shaders.LoadCompute("ParticleFinalize");
		skinningProgram = shaders.LoadCompute("Skinning");

		// One bindless space (samplers + textures) shared by Forward+, masked shadows,
		// particles and UI; every layout defines it identically.
		const auto bindlessSpace = R::ForwardPlusBindlessSpace(desc.BindlessTextures, desc.BindlessSamplers);
		const auto shadowSpace = R::ShadowBindlessSpace(desc.BindlessTextures, desc.BindlessSamplers);
		forwardOpaque = LoadDrawProgram("ForwardOpaque", &bindlessSpace);
		forwardTransparent = LoadDrawProgram("ForwardTransparent", &bindlessSpace);
		forwardDepth = LoadDrawProgram("ForwardDepth", &bindlessSpace);
		forwardPrepassed = LoadDrawProgram("ForwardOpaquePrepassed", &bindlessSpace);
		shadowDepth = LoadDrawProgram("ShadowDepth", nullptr);
		shadowMasked = LoadDrawProgram("ShadowMasked", &shadowSpace);
		particleRender = LoadDrawProgram("ParticleRender", &bindlessSpace);
		uiQuad = LoadDrawProgram("UiQuad", &bindlessSpace);
		skyBackground = LoadDrawProgram("SkyBackground", nullptr);
		present = LoadDrawProgram("Present", nullptr);

		const auto require = [](auto pointer, const char* what)
		{
			if (!pointer)
			{
				throw std::runtime_error(std::string("FrameRenderer: cannot create the ") + what + " pipeline");
			}

			return pointer;
		};
		forwardOpaquePipeline = require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::PipelineDesc(
											R::ForwardPlusBin::Opaque, *forwardOpaque.Program, *forwardOpaque.Layout)),
			"Forward+ opaque");
		forwardTransparentPipeline = require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::PipelineDesc(
												 R::ForwardPlusBin::Transparent, *forwardTransparent.Program, *forwardTransparent.Layout)),
			"Forward+ transparent");
		forwardDepthPipeline = require(
			device.CreateGraphicsPipeline(R::ForwardPlusRenderer::DepthPrepassPipelineDesc(*forwardDepth.Program, *forwardDepth.Layout)),
			"Forward+ depth prepass");
		forwardPrepassedPipeline = require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::PrepassedPipelineDesc(
											   *forwardPrepassed.Program, *forwardPrepassed.Layout)),
			"Forward+ prepassed opaque");
		shadowDepthPipeline = require(
			device.CreateGraphicsPipeline(R::ShadowRenderer::PipelineDesc(*shadowDepth.Program, *shadowDepth.Layout)), "shadow depth");
		shadowMaskedPipeline = require(
			device.CreateGraphicsPipeline(R::ShadowRenderer::PipelineDesc(*shadowMasked.Program, *shadowMasked.Layout)), "masked shadow");
		particleAdditive = require(device.CreateGraphicsPipeline(R::ParticleSystem::PipelineDesc(
									   R::ParticleBlendMode::Additive, *particleRender.Program, *particleRender.Layout)),
			"additive particle");
		particleAlpha = require(device.CreateGraphicsPipeline(R::ParticleSystem::PipelineDesc(
									R::ParticleBlendMode::AlphaBlend, *particleRender.Program, *particleRender.Layout)),
			"alpha particle");
		uiPipeline = require(
			device.CreateGraphicsPipeline(R::UiRenderer::PipelineDesc(S::Format::RGBA8Unorm, *uiQuad.Program, *uiQuad.Layout)), "UI");
		uiDepthPipeline = require(device.CreateGraphicsPipeline(R::UiRenderer::PipelineDesc(
									  S::Format::RGBA8Unorm, *uiQuad.Program, *uiQuad.Layout, S::Format::D32Float)),
			"world UI");
		{
			// The sky pass writes the colour target and clears depth (the opaque pass clears the
			// other Forward+ targets: ForwardPlusTargets::ClearAuxiliary).
			static const std::array<S::Format, 1> formats{ R::ForwardPlusRenderer::ColorFormat };
			S::GraphicsPipelineDesc skyDesc{};
			skyDesc.Program = skyBackground.Program.get();
			skyDesc.Layout = skyBackground.Layout.get();
			skyDesc.ColorFormats = formats;
			skyDesc.DepthStencilFormat = R::CanonicalDepthFormat;
			skyDesc.DepthStencil.DepthTest = false;
			skyDesc.DepthStencil.DepthWrite = false;
			skyDesc.Raster.Cull = S::CullMode::None;
			skyDesc.DebugName = "Sky background";
			skyPipeline = require(device.CreateGraphicsPipeline(skyDesc), "sky background");
		}

		// --- Shared resources ------------------------------------------------------
		{
			S::TextureDesc white{};
			white.Extent = { 1, 1, 1 };
			white.PixelFormat = S::Format::RGBA8Unorm;
			white.Usage = S::TextureUsage::Sampled | S::TextureUsage::TransferDestination;
			white.DebugName = "Bindless fallback";
			whiteTexture = require(device.CreateTexture(white), "fallback texture");
			S::TextureViewDesc view{};
			view.PixelFormat = white.PixelFormat;
			whiteView = require(device.CreateTextureView(*whiteTexture, view), "fallback view");
		}
		{
			S::SamplerDesc repeat{};
			repeat.EnableAnisotropy = true;
			repeat.MaxAnisotropy = 8.0f;
			repeat.DebugName = "Material sampler";
			linearRepeat = device.CreateSampler(repeat);

			if (!linearRepeat)
			{
				repeat.EnableAnisotropy = false;
				linearRepeat = require(device.CreateSampler(repeat), "material sampler");
			}

			S::SamplerDesc clamp{};
			clamp.AddressU = clamp.AddressV = clamp.AddressW = S::SamplerAddressMode::ClampToEdge;
			clamp.DebugName = "Linear clamp";
			linearClamp = require(device.CreateSampler(clamp), "linear clamp sampler");
			S::SamplerDesc presentDesc = clamp;
			presentDesc.MinFilter = presentDesc.MagFilter = S::Filter::Nearest;
			presentDesc.MipFilter = S::Filter::Nearest;
			presentDesc.DebugName = "Present sampler";
			presentSampler = require(device.CreateSampler(presentDesc), "present sampler");
		}
		R::BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = forwardOpaque.Layout.get();
		bindlessDesc.Space = R::ForwardPlusDrawBindings::BindlessSpace;
		bindlessDesc.FallbackTexture = whiteView.get();
		bindlessDesc.FallbackSampler = linearRepeat.get();
		bindlessDesc.DebugName = "Runtime bindless";
		bindless = std::make_unique<R::BindlessResourceTable>(device, bindlessDesc);
		materialSampler = bindless->GetIndex(bindless->RegisterSampler(*linearRepeat));

		// --- Residency -------------------------------------------------------------
		R::GeometryHeapDesc heapDesc;
		heapDesc.VertexPageSize = desc.VertexPageSize;
		heapDesc.IndexPageSize = desc.IndexPageSize;
		heapDesc.MeshletPageSize = 4ull << 20;
		heapDesc.MaxMeshes = 4096;
		heapDesc.MaxSubmeshes = 16384;
		heapDesc.MaxPages = 64;
		heapDesc.DebugName = "Runtime geometry";
		geometry = std::make_unique<R::GeometryHeap>(device, heapDesc);
		textures = std::make_unique<R::TextureResidency>(device, R::TextureResidencyDesc{ desc.BindlessTextures, "Runtime textures" });
		R::AssetResidencyDesc residencyDesc;
		residencyDesc.Bindless = bindless.get();
		residencyDesc.RetainCpuAssets = false;
		residency = std::make_unique<R::AssetResidencyService>(assets, io, jobs, *geometry, *textures, residencyDesc);

		materialTemplate = R::CreateStandardMaterialTemplate();
		materialTable = std::make_unique<R::GpuMaterialTable>(
			device, R::GpuMaterialTableDesc{ materialTemplate, desc.MaxMaterials, "Runtime materials" });
		materials = std::make_unique<MaterialLibrary>(*materialTable, materialTemplate, *residency, materialSampler);
		meshes = std::make_unique<MeshLibrary>(assets, *residency, *geometry);

		scene = std::make_unique<R::GpuScene>(device, R::GpuSceneDesc{ desc.MaxObjects, "Runtime scene" });
		lights = std::make_unique<R::GpuLightBuffer>(
			device, R::GpuLightBufferDesc{ desc.MaxDirectionalLights, desc.MaxLocalLights, "Runtime lights" });

		R::ParticleSystemDesc particleDesc;
		particleDesc.Capacity = desc.MaxParticles;
		particleDesc.MaxEmitters = desc.MaxEmitters;
		particleDesc.Simulate = { particleSimulate.Pipeline.get(), particleSimulate.Layout.get() };
		particleDesc.Emit = { particleEmit.Pipeline.get(), particleEmit.Layout.get() };
		particleDesc.Compact = { particleCompact.Pipeline.get(), particleCompact.Layout.get() };
		particleDesc.Finalize = { particleFinalize.Pipeline.get(), particleFinalize.Layout.get() };
		particles = std::make_unique<R::ParticleSystem>(device, particleDesc);

		R::SkinningSystemDesc skinDesc;
		skinDesc.Pipeline = skinningProgram.Pipeline.get();
		skinDesc.Layout = skinningProgram.Layout.get();
		skinDesc.MaxSourceVertices = 1u << 18;
		skinDesc.MaxMorphDeltas = 1u << 16;
		skinDesc.MaxMeshes = 64;
		skinDesc.MaxInstances = 256;
		skinning = std::make_unique<R::SkinningSystem>(device, *geometry, skinDesc);

		// --- Frame subsystems ------------------------------------------------------
		R::ForwardPlusRendererDesc forwardDesc;
		forwardDesc.Opaque = { forwardOpaquePipeline.get(), forwardOpaque.Layout.get() };
		forwardDesc.Transparent = { forwardTransparentPipeline.get(), forwardTransparent.Layout.get() };
		forwardDesc.DepthPrepass = { forwardDepthPipeline.get(), forwardDepth.Layout.get() };
		forwardDesc.OpaquePrepassed = { forwardPrepassedPipeline.get(), forwardPrepassed.Layout.get() };

		if (shaders.Contains("ForwardBackDepth"))
		{
			forwardBackDepth = LoadDrawProgram("ForwardBackDepth", &bindlessSpace);
			forwardBackDepthPipeline = require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::DepthPrepassPipelineDesc(
												   *forwardBackDepth.Program, *forwardBackDepth.Layout)),
				"Forward+ back depth");
			forwardDesc.BackDepth = { forwardBackDepthPipeline.get(), forwardBackDepth.Layout.get() };
		}

		if (shaders.Contains("ForwardOpaqueDeferred") && shaders.Contains("ForwardLocalLights"))
		{
			forwardDeferred = LoadDrawProgram("ForwardOpaqueDeferred", &bindlessSpace);
			forwardDeferredPipeline = require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::DeferredPipelineDesc(
												  *forwardDeferred.Program, *forwardDeferred.Layout)),
				"Forward+ deferred opaque");
			forwardLocalLights = shaders.LoadCompute("ForwardLocalLights");
			forwardDesc.OpaqueDeferred = { forwardDeferredPipeline.get(), forwardDeferred.Layout.get() };
			forwardDesc.LocalLightsPipeline = forwardLocalLights.Pipeline.get();
			forwardDesc.LocalLightsLayout = forwardLocalLights.Layout.get();
		}

		forwardDesc.SortPipeline = sortProgram.Pipeline.get();
		forwardDesc.SortLayout = sortProgram.Layout.get();
		forwardDesc.DrawPath = drawPath;
		forward = std::make_unique<R::ForwardPlusRenderer>(device, forwardDesc);

		R::ShadowRendererDesc shadowDesc;
		shadowDesc.Opaque = { shadowDepthPipeline.get(), shadowDepth.Layout.get() };
		shadowDesc.Masked = { shadowMaskedPipeline.get(), shadowMasked.Layout.get() };

		if (shaders.Contains("ShadowClear"))
		{
			shadowClear = LoadDrawProgram("ShadowClear", nullptr);
			shadowClearPipeline = require(
				device.CreateGraphicsPipeline(R::ShadowRenderer::ClearPipelineDesc(*shadowClear.Program, *shadowClear.Layout)), "shadow clear");
			shadowDesc.Clear = { shadowClearPipeline.get(), shadowClear.Layout.get() };
		}

		shadowDesc.DrawPath = drawPath;
		shadows = std::make_unique<R::ShadowRenderer>(shadowDesc);

		R::ClusteredLightAssignerDesc clusterDesc;
		clusterDesc.Cull = { clusterCull.Pipeline.get(), clusterCull.Layout.get(), clusterCull.Space };
		clusterDesc.Bounds = { clusterBounds.Pipeline.get(), clusterBounds.Layout.get(), clusterBounds.Space };
		clusterDesc.Assign = { clusterAssign.Pipeline.get(), clusterAssign.Layout.get(), clusterAssign.Space };
		clusterDesc.Scan = { clusterScan.Pipeline.get(), clusterScan.Layout.get(), clusterScan.Space };
		clusters = std::make_unique<R::ClusteredLightAssigner>(clusterDesc);

		R::EnvironmentBuilderDesc environmentDesc;
		environmentDesc.Sky = { environmentSky.Pipeline.get(), environmentSky.Layout.get(), environmentSky.Space };
		environmentDesc.Downsample = { environmentDownsample.Pipeline.get(), environmentDownsample.Layout.get(),
			environmentDownsample.Space };
		environmentDesc.Prefilter = { environmentPrefilter.Pipeline.get(), environmentPrefilter.Layout.get(), environmentPrefilter.Space };
		environmentDesc.Irradiance = { environmentIrradiance.Pipeline.get(), environmentIrradiance.Layout.get(),
			environmentIrradiance.Space };
		environmentDesc.BrdfLut = { environmentLut.Pipeline.get(), environmentLut.Layout.get(), environmentLut.Space };
		environmentDesc.Sampler = linearClamp.get();

		if (shaders.Contains("EnvironmentOverlay"))
		{
			environmentOverlay = shaders.LoadCompute("EnvironmentOverlay");
			environmentDesc.Overlay = { environmentOverlay.Pipeline.get(), environmentOverlay.Layout.get(), environmentOverlay.Space };
		}

		environmentBuilder = std::make_unique<R::EnvironmentBuilder>(environmentDesc);

		R::ScreenSpaceEffectsDesc ssDesc;
		ssDesc.AmbientOcclusion = { ssAo.Pipeline.get(), ssAo.Layout.get(), ssAo.Space };
		ssDesc.Blur = { ssBlur.Pipeline.get(), ssBlur.Layout.get(), ssBlur.Space };
		ssDesc.Composite = { ssComposite.Pipeline.get(), ssComposite.Layout.get(), ssComposite.Space };

		if (ssReflectionTemporal.Pipeline)
		{
			ssDesc.ReflectionTemporal = { ssReflectionTemporal.Pipeline.get(), ssReflectionTemporal.Layout.get(), ssReflectionTemporal.Space };
		}

		ssDesc.Reflection = { ssReflection.Pipeline.get(), ssReflection.Layout.get(), ssReflection.Space };
		ssDesc.ProbeSampler = linearClamp.get();
		screenSpace = std::make_unique<R::ScreenSpaceEffects>(ssDesc);

		if (shaders.Contains("ReflectionProbeResolve") && shaders.Contains("ReflectionProbePrefilter"))
		{
			probeResolve = shaders.LoadCompute("ReflectionProbeResolve");
			probePrefilter = shaders.LoadCompute("ReflectionProbePrefilter");
			R::ReflectionProbeRendererDesc probeDesc;
			probeDesc.Resolve = { probeResolve.Pipeline.get(), probeResolve.Layout.get(), probeResolve.Space };
			probeDesc.Downsample = { environmentDownsample.Pipeline.get(), environmentDownsample.Layout.get(), environmentDownsample.Space };
			probeDesc.Prefilter = { probePrefilter.Pipeline.get(), probePrefilter.Layout.get(), probePrefilter.Space };
			probeDesc.Sampler = linearClamp.get();
			probeRenderer = std::make_unique<R::ReflectionProbeRenderer>(device, probeDesc);
		}

		if (shaders.Contains("PlanarReflectionResolve"))
		{
			planarResolve = shaders.LoadCompute("PlanarReflectionResolve");
			R::PlanarReflectionRendererDesc planarDesc;
			planarDesc.Resolve = { planarResolve.Pipeline.get(), planarResolve.Layout.get(), planarResolve.Space };
			planarDesc.Sampler = linearClamp.get();
			planarRenderer = std::make_unique<R::PlanarReflectionRenderer>(device, planarDesc);
		}

		temporal = std::make_unique<R::TemporalAntiAliasing>(device,
			R::TemporalAntiAliasingDesc{ { temporalResolve.Pipeline.get(), temporalResolve.Layout.get(), temporalResolve.Space }, "TAA" });

		R::PostProcessorDesc postDesc;
		postDesc.Histogram = { postHistogram.Pipeline.get(), postHistogram.Layout.get(), postHistogram.Space };
		postDesc.Exposure = { postExposure.Pipeline.get(), postExposure.Layout.get(), postExposure.Space };
		postDesc.BloomDownsample = { postBloomDown.Pipeline.get(), postBloomDown.Layout.get(), postBloomDown.Space };
		postDesc.BloomUpsample = { postBloomUp.Pipeline.get(), postBloomUp.Layout.get(), postBloomUp.Space };
		postDesc.Composite = { postComposite.Pipeline.get(), postComposite.Layout.get(), postComposite.Space };
		postDesc.CompositeHdr = { postCompositeHdr.Pipeline.get(), postCompositeHdr.Layout.get(), postCompositeHdr.Space };
		post = std::make_unique<R::PostProcessor>(device, postDesc);

		ui = std::make_unique<R::UiRenderer>();
		atlasTextures = std::make_unique<R::UiAtlasTextures>(device, *bindless);

		materials->SetRouter(
			[this](std::uint32_t set, const R::StandardPbr::Parameters& parameters)
			{
				Route(set, parameters);
			});
	}

	FrameRenderer::Impl::~Impl()
	{
		// Every GPU object must be idle before its owner goes.
		try
		{
			renderDevice.WaitIdle();
		}
		catch (...)
		{
		}

		if (atlasTextures)
		{
			atlasTextures->Drain();
		}
	}

	RuntimeGraphicsProgram FrameRenderer::Impl::LoadDrawProgram(std::string_view name, const S::DescriptorSchemaDesc* bindlessSpace)
	{
		if (bindlessSpace)
		{
			return shaders.LoadGraphics(name, { bindlessSpace, 1 });
		}

		return shaders.LoadGraphics(name);
	}

	S::GraphicsPipeline& FrameRenderer::Impl::GetPresentPipeline(S::Format format)
	{
		auto& pipeline = presentPipelines[format];

		if (!pipeline)
		{
			S::GraphicsPipelineDesc presentDesc{};
			presentDesc.Program = present.Program.get();
			presentDesc.Layout = present.Layout.get();
			presentDesc.ColorFormats = { &format, 1 };
			presentDesc.DepthStencil.DepthTest = false;
			presentDesc.DepthStencil.DepthWrite = false;
			presentDesc.Raster.Cull = S::CullMode::None;
			presentDesc.DebugName = "Present";
			pipeline = device.CreateGraphicsPipeline(presentDesc);

			if (!pipeline)
			{
				throw std::runtime_error("FrameRenderer: cannot create the present pipeline");
			}
		}

		return *pipeline;
	}

	void FrameRenderer::Impl::Route(std::uint32_t set, const R::StandardPbr::Parameters& parameters)
	{
		routes[set] = parameters;

		if (visibility)
		{
			R::ForwardPlusRenderer::RouteMaterial(*visibility, set, parameters);
		}

		if (shadowVisibility)
		{
			R::ShadowRenderer::RouteMaterial(*shadowVisibility, set, parameters);
		}

		if (probeVisibility)
		{
			R::ForwardPlusRenderer::RouteMaterial(*probeVisibility, set, parameters);
		}

		if (planarVisibility)
		{
			R::ForwardPlusRenderer::RouteMaterial(*planarVisibility, set, parameters);
		}
	}

	void FrameRenderer::Impl::EnsureVisibility(std::uint32_t slots)
	{
		if (visibility && visibilitySlots == slots)
		{
			return;
		}

		R::GpuVisibilityDesc visibilityDesc;
		visibilityDesc.CullPipeline = visibilityProgram.Pipeline.get();
		visibilityDesc.Layout = visibilityProgram.Layout.get();
		visibilityDesc.Space = visibilityProgram.Space;
		visibilityDesc.MaxObjects = desc.MaxObjects;
		visibilityDesc.MaxMaterialSets = desc.MaxMaterials;
		visibilityDesc.MaterialBinCapacities = R::ForwardPlusRenderer::VisibilityBinCapacities(desc.MaxObjects, 4096);
		visibilityDesc.IndexPageSlots = slots;
		visibilityDesc.DebugName = "Main visibility";
		visibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilityDesc.DebugName = "Probe visibility";
		probeVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilityDesc.DebugName = "Planar visibility";
		planarVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilityDesc.MaterialBinCapacities = R::ShadowRenderer::VisibilityBinCapacities(desc.MaxObjects, 4096);
		visibilityDesc.DebugName = "Shadow visibility";
		shadowVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilitySlots = slots;

		for (const auto& [set, parameters] : routes)
		{
			R::ForwardPlusRenderer::RouteMaterial(*visibility, set, parameters);
			R::ForwardPlusRenderer::RouteMaterial(*probeVisibility, set, parameters);
			R::ForwardPlusRenderer::RouteMaterial(*planarVisibility, set, parameters);
			R::ShadowRenderer::RouteMaterial(*shadowVisibility, set, parameters);
		}
	}

	void FrameRenderer::Impl::EnsureEnvironmentTargets(std::uint32_t resolution)
	{
		if (prefiltered && environmentMap.SourceSize == resolution)
		{
			return;
		}

		environmentMap = {};
		environmentMap.SourceSize = resolution;
		environmentMap.PrefilteredSize = std::max(resolution / 2, 16u);
		environmentMap.PrefilteredMipCount = 5;
		environmentMap.PrefilterSampleCount = 64;
		environmentMap.IrradianceFaceSize = std::min(16u, resolution);
		R::EnvironmentBuilder::Validate(environmentMap);
		prefiltered = device.CreateTexture(R::EnvironmentBuilder::PrefilteredCubeDesc(environmentMap));
		irradiance = device.CreateBuffer(R::EnvironmentBuilder::IrradianceBufferDesc());

		if (!prefiltered || !irradiance)
		{
			throw std::runtime_error("FrameRenderer: cannot create the environment maps");
		}

		builtSky.reset();
		environmentValid = false;
	}

	// ----------------------------------------------------------------------------------

	FrameRenderer::FrameRenderer(RenderDevice& deviceValue, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io,
		Swim::Jobs::JobSystem* jobs, const FrameRendererDesc& desc)
		: device(deviceValue), impl(std::make_unique<Impl>(deviceValue, assets, io, jobs, desc))
	{
		settings.Sanitize();
	}

	FrameRenderer::~FrameRenderer()
	{
		impl.reset();
	}

	MeshLibrary& FrameRenderer::GetMeshes() const
	{
		return *impl->meshes;
	}

	MaterialLibrary& FrameRenderer::GetMaterials() const
	{
		return *impl->materials;
	}

	R::GpuScene& FrameRenderer::GetScene() const
	{
		return *impl->scene;
	}

	R::GpuLightBuffer& FrameRenderer::GetLights() const
	{
		return *impl->lights;
	}

	R::ParticleSystem& FrameRenderer::GetParticles() const
	{
		return *impl->particles;
	}

	R::SkinningSystem& FrameRenderer::GetSkinning() const
	{
		return *impl->skinning;
	}

	R::BindlessResourceTable& FrameRenderer::GetBindless() const
	{
		return *impl->bindless;
	}

	R::AssetResidencyService& FrameRenderer::GetResidency() const
	{
		return *impl->residency;
	}

	R::GeometryHeap& FrameRenderer::GetGeometry() const
	{
		return *impl->geometry;
	}

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
		impl->residency->Update();
		impl->materials->Update();
	}

	void FrameRenderer::GatherTimings()
	{
		auto& executor = device.GetExecutor();
		executor.Wait();

		// Timings of the frame that just completed (none before the first submission).
		std::vector<R::GraphPassTiming> timings;

		if (lastCompletion.Semaphore)
		{
			try
			{
				timings = executor.ReadTimings();
			}
			catch (const std::exception&)
			{
				timings.clear(); // The last graph failed or was never executed.
			}
		}

		// Each pass's exclusive GPU time: the step of its end timestamp (begin timestamps are
		// written before the pass's barriers and can overlap earlier work, so summing the
		// begin-to-end spans counts overlap twice). The last end is the frame's GPU time.
		std::vector<RenderStats::PassTiming> passes;
		double total = 0.0;
		double previousEnd = 0.0;
		bool haveEnds = true;

		for (const auto& timing : timings)
		{
			haveEnds = haveEnds && timing.EndOffsetNanoseconds.has_value();
		}

		for (const auto& timing : timings)
		{
			if (haveEnds)
			{
				const double end = *timing.EndOffsetNanoseconds * 1.0e-6;
				const double ms = std::max(end - previousEnd, 0.0);
				previousEnd = std::max(previousEnd, end);
				passes.push_back({ timing.Name, ms });
				total = previousEnd;
			}
			else if (timing.Nanoseconds)
			{
				const double ms = *timing.Nanoseconds * 1.0e-6;
				total += ms;
				passes.push_back({ timing.Name, ms });
			}
		}

		stats.GpuPasses = passes;
		stats.GpuTimingsAvailable = !passes.empty();
		stats.GpuMilliseconds = total;
		stats.Passes = static_cast<std::uint32_t>(timings.size());
		std::sort(passes.begin(), passes.end(),
			[](const auto& a, const auto& b)
			{
				return a.Milliseconds > b.Milliseconds;
			});
		stats.TopPassCount = static_cast<std::uint32_t>(std::min(passes.size(), stats.TopPasses.size()));

		for (std::uint32_t i = 0; i < stats.TopPassCount; ++i)
		{
			stats.TopPasses[i] = passes[i];
		}
	}

	bool FrameRenderer::Render(const RenderFrameInput& input)
	{
		using Clock = std::chrono::steady_clock;
		const auto cpuStart = Clock::now();
		auto& I = *impl;
		// This frame's slot: its previous frame (FramesInFlight frames ago) must be complete
		// before the slot's executor, staging and acquire semaphore are reused (GatherTimings
		// waits for it). With two slots the GPU still renders the last frame while this one
		// is built and recorded.
		device.AdvanceFrameSlot();
		auto& executor = device.GetExecutor();
		settings.Sanitize();
		stats.CpuPhases.clear();
		auto lapStart = cpuStart;
		const auto lap = [&](const char* name)
		{
			const auto now = Clock::now();
			stats.CpuPhases.push_back({ name, std::chrono::duration<double, std::milli>(now - lapStart).count() });
			lapStart = now;
		};
		GatherTimings();
		lap("Wait for the previous frame (GPU)");

		auto frame = device.Acquire();
		lap("Acquire");
		const auto extent = device.GetExtent();

		if (!frame.Valid || extent.Width == 0 || extent.Height == 0)
		{
			++stats.SkippedFrames;
			stats.Presented = false;
			return false;
		}

		const std::uint32_t width = extent.Width;
		const std::uint32_t height = extent.Height;

		const auto& camera = input.Camera;

		if (camera.Cut)
		{
			I.reflectionHistoryValid = false;
			I.temporal->ResetHistory();
			I.previousViewProjection.reset();
		}

		const bool temporalOn = settings.TemporalAntiAliasing;

		if (!temporalOn)
		{
			I.temporal->ResetHistory();
		}

		const std::array<float, 2> jitter =
			temporalOn ? I.temporal->GetJitterNdc(settings.Temporal, width, height) : std::array<float, 2>{ 0.0f, 0.0f };
		const auto viewProjection = R::MultiplyRowMajor(camera.Projection, camera.View);

		// Which GeometryHeap pages this frame's draws come from.
		bool pageConflict = false;
		const auto pageSlots = I.meshes->CollectPageSlots(&pageConflict);
		const bool draw3D = !pageSlots.empty();
		std::vector<R::ForwardPlusPageSlot> forwardSlots;
		std::vector<R::ShadowPageSlot> shadowSlots;
		std::vector<std::uint32_t> indexPages;

		for (const auto& slot : pageSlots)
		{
			forwardSlots.push_back({ slot.IndexPage, slot.VertexPage });
			shadowSlots.push_back({ slot.IndexPage, slot.VertexPage });
			indexPages.push_back(slot.IndexPage);
		}

		if (draw3D)
		{
			I.EnsureVisibility(static_cast<std::uint32_t>(pageSlots.size()));
		}

		R::RenderGraph graph;
		bool environmentRebuilt = false, environmentWithFeatures = false;
		bool residencyImported = false, materialsImported = false, sceneImported = false, lightsImported = false;
		bool particlesPending = false, skinningPending = false, atlasPending = false;
		std::optional<R::GraphReadback> captureReadback;
		S::TimelinePoint completion{};

		try
		{
			// --- Uploads and persistent imports ---------------------------------------
			if (!I.whiteUploaded)
			{
				const auto white = graph.ImportTexture(*I.whiteTexture, ResourceState::Undefined);
				static const std::array<std::uint8_t, 4> texel{ 255, 255, 255, 255 };
				R::AddTextureUpload(graph, "Fallback texture", std::as_bytes(std::span(texel)), white, { 0, {}, {}, { 1, 1, 1 } });
				graph.Export(white, ResourceState::ShaderRead);
			}

			const auto residencyResources = I.residency->Import(graph);
			residencyImported = true;
			const auto& geometryResources = residencyResources.Geometry;

			if (I.skinning->GetStats().Instances > 0 || I.skinning->GetStats().PendingMeshes > 0)
			{
				I.skinning->Record(graph, geometryResources);
				skinningPending = true;
			}

			const auto materialResources = I.materialTable->Import(graph);
			materialsImported = true;
			const auto sceneResources = I.scene->Import(graph);
			sceneImported = true;
			const auto lightResources = I.lights->Import(graph);
			lightsImported = true;

			// The view render features see (the features themselves run further down; the
			// environment below also asks them for their contribution).
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
			I.featureTime += frameSeconds;
			featureView.Time = static_cast<float>(I.featureTime);
			featureView.DeltaTime = frameSeconds;
			featureView.Frame = static_cast<std::uint32_t>(I.frameIndex);
			const auto featureServices = [this]
			{
				RenderFeatureContext::Services services;
				services.LoadCompute = [this](std::string_view name) -> const RuntimeComputeProgram&
				{
					auto& slot = impl->featurePrograms[std::string(name)];

					if (!slot)
					{
						slot = std::make_unique<RuntimeComputeProgram>(impl->shaders.LoadCompute(name));
					}

					return *slot;
				};
				services.GetSampler = [this](std::string_view kind) -> S::Sampler&
				{
					if (kind == "LinearRepeat")
					{
						return *impl->linearRepeat;
					}

					if (kind == "PointClamp")
					{
						return *impl->presentSampler;
					}

					return *impl->linearClamp;
				};
				return services;
			};

			lap("Build: Uploads and imports");
			// --- Environment ------------------------------------------------------------
			std::optional<R::EnvironmentGraphResources> environment;
			std::optional<R::GraphTexture> lut;

			if (!I.brdfLut)
			{
				I.brdfLut = device.GetDevice().CreateTexture(R::EnvironmentBuilder::BrdfLutDesc(Impl::LutSize));

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
						RenderFeatureContext context(graph, RenderFeatureStage::BeforeTemporal, featureView, settings, {}, {}, featureServices());

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

			lap("Build: Environment");
			// --- Frame targets ----------------------------------------------------------
			// A complete set of Forward+ targets; the main view uses the viewport, reflection
			// probe captures small squares.
			const auto makeTargets = [&](std::uint32_t targetWidth, std::uint32_t targetHeight, const char* prefix)
			{
				const auto target = [&](S::Format format, S::TextureUsage usage, const char* name)
				{
					S::TextureDesc textureDesc;
					textureDesc.Extent = { targetWidth, targetHeight, 1 };
					textureDesc.PixelFormat = format;
					textureDesc.Usage = usage;
					const std::string debugName = std::string(prefix) + name;
					textureDesc.DebugName = debugName;
					return graph.CreateTexture(textureDesc);
				};
				const auto attachmentSampled = S::TextureUsage::ColorAttachment | S::TextureUsage::Sampled;
				R::ForwardPlusTargets set;
				// Storage: the deferred local lights add to it in a compute pass.
				set.Color = target(R::ForwardPlusRenderer::ColorFormat,
					attachmentSampled | S::TextureUsage::TransferSource | S::TextureUsage::Storage, "Scene color");
				set.ObjectId =
					target(R::ForwardPlusRenderer::ObjectIdFormat, attachmentSampled | S::TextureUsage::TransferSource, "Object id");
				set.Depth = target(R::CanonicalDepthFormat, S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled, "Scene depth");
				set.Velocity = target(R::ForwardPlusRenderer::VelocityFormat, attachmentSampled, "Velocity");
				set.Normal = target(R::ForwardPlusRenderer::NormalFormat, attachmentSampled, "Normal");
				set.Indirect = target(R::ForwardPlusRenderer::IndirectFormat, attachmentSampled, "Indirect");
				set.Reflectance = target(R::ForwardPlusRenderer::ReflectanceFormat, attachmentSampled, "Reflectance");
				set.Specular = target(R::ForwardPlusRenderer::SpecularFormat, attachmentSampled, "Specular");
				set.Clear = false;			// The sky pass clears colour and depth,
				set.ClearAuxiliary = true;	// the opaque pass every other target.
				return set;
			};
			const auto target = [&](S::Format format, S::TextureUsage usage, const char* name)
			{
				S::TextureDesc textureDesc;
				textureDesc.Extent = { width, height, 1 };
				textureDesc.PixelFormat = format;
				textureDesc.Usage = usage;
				textureDesc.DebugName = name;
				return graph.CreateTexture(textureDesc);
			};
			R::ForwardPlusTargets targets = makeTargets(width, height, "");
			const bool backDepthOn =
				draw3D && settings.ScreenSpace.Reflections.Enabled && settings.ScreenSpace.Reflections.BackFaces && I.forward->SupportsBackDepth();

			if (backDepthOn)
			{
				S::TextureDesc backDesc;
				backDesc.Extent = { width, height, 1 };
				backDesc.PixelFormat = R::CanonicalDepthFormat;
				backDesc.Usage = S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled;
				backDesc.DebugName = "Back-face depth";
				targets.BackDepth = graph.CreateTexture(backDesc);
			}

			lap("Build: Frame targets");
			// --- Sky background (clears every Forward+ target) ---------------------------
			// rayRight/rayUp are the view's right and up scaled by the half-angle tangents.
			const auto recordSky = [&](const R::ForwardPlusTargets& skyTargets, const std::array<float, 3>& rayRight,
									   const std::array<float, 3>& rayUp, const std::array<float, 3>& rayForward,
									   std::uint32_t skyWidth, std::uint32_t skyHeight, const char* passName = "Sky background")
			{
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
					sky.Ground[c] = drawSky ? (settings.SkyBackgroundGround ? (*settings.SkyBackgroundGround)[c] : s.GroundColor[c])
											: settings.ClearColor[c];
					sky.SunDirection[c] = sun[c];
					sky.SunColor[c] = drawSky ? s.SunColor[c] : 0.0f;
				}

				sky.RayRight[3] = drawSky ? s.Intensity * settings.EnvironmentIntensity : 1.0f;
				sky.RayUp[3] = settings.EnvironmentRotation;
				sky.RayForward[3] = std::max(s.SunSharpness, 1.0f);
				sky.Ground[3] = s.GroundFalloff;
				auto* pipeline = I.skyPipeline.get();
				const auto set = skyTargets;
				graph.AddPass(
					passName, S::QueueType::Graphics,
					[&](R::RenderGraphBuilder& b)
					{
						b.Write(set.Color, ResourceState::ColorAttachment);
						b.Write(set.Depth, ResourceState::DepthStencilWrite);
					},
					[set, sky, pipeline, skyWidth, skyHeight](R::RenderCommandContext& c)
					{
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
						list.SetViewport({ 0, 0, float(skyWidth), float(skyHeight) });
						list.SetScissor({ 0, 0, skyWidth, skyHeight });
						list.PushConstants(S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&sky, 1)));
						list.Draw(3);
						list.EndRendering();
					});
			};
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

				recordSky(targets, right, up, camera.Forward, width, height);
			}

			R::RenderViewDesc viewDesc;
			viewDesc.ViewProjection = viewProjection;
			viewDesc.CameraPosition = camera.Position;

			stats.ShadowViews = 0;
			stats.ShadowCasters = static_cast<std::uint32_t>(input.ShadowCasters.size());
			stats.Rendered3D = draw3D;
			stats.PageSlots = static_cast<std::uint32_t>(pageSlots.size());
			std::optional<R::ForwardPlusGraphResources> forwardResources;
			std::optional<R::ScreenSpaceFrame::ProbeInputs> probeInputs;
			std::optional<R::ScreenSpaceFrame::PlanarInputs> planarInputs;
			stats.ReflectionProbes = 0;
			stats.ReflectionProbeFaces = 0;
			stats.PlanarReflections = 0;
			stats.PlanarCaptures = 0;
			stats.PlanarCandidates = 0;

			if (draw3D)
			{
				lap("Build: Sky and scene setup");
				// --- Shadows ------------------------------------------------------------
				std::optional<R::ShadowGraphResources> shadowResources;

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
					const auto plan =
						std::make_shared<R::ShadowPlan>(R::PlanShadows(settings.Shadow, shadowCamera, input.ShadowCasters, *I.atlas));

					if (!plan->Draws.empty())
					{
						R::ShadowFrame shadowFrame;
						shadowFrame.Scene = &sceneResources;
						shadowFrame.Geometry = &geometryResources;
						shadowFrame.Visibility = I.shadowVisibility.get();
						shadowFrame.PageSlots = shadowSlots;
						shadowFrame.Materials = &materialResources;
						shadowFrame.Bindless = &I.bindless->GetTable();
						shadowFrame.Plan = plan.get();
						shadowFrame.ZeroUnusedCommands = R::NeedsZeroedCommands(I.drawPath);
						std::optional<R::GraphTexture> cachedAtlas;

						if (settings.ShadowCascadeCache && I.shadows->SupportsPersistentAtlas())
						{
							cachedAtlas = I.PlanShadowCache(graph, *plan, shadowFrame, camera, input.ShadowCasters);
						}
						else
						{
							I.shadowCache.clear();
						}

						shadowResources = I.shadows->Record(graph, shadowFrame);

						if (cachedAtlas)
						{
							graph.Export(*cachedAtlas, ResourceState::ShaderRead);
						}

						stats.ShadowViews = plan->Stats.Views;
					}
				}

				lap("Build: Shadows");
				// --- Visibility and clusters --------------------------------------------
				R::VisibilityFrameDesc visibilityFrame;
				visibilityFrame.View = R::BuildGpuViewRecord(viewDesc);
				visibilityFrame.IndexPages = indexPages;
				visibilityFrame.ReadStats = false;
				visibilityFrame.ZeroUnusedCommands = R::NeedsZeroedCommands(I.drawPath);
				const auto visible = I.visibility->Record(graph, sceneResources, geometryResources, visibilityFrame);

				R::ClusterGridDesc grid;
				grid.ViewportWidth = width;
				grid.ViewportHeight = height;
				grid.TileSize = settings.ClusterTileSize;
				grid.SliceCount = settings.ClusterSlices;
				grid.Near = camera.Near;
				grid.Far = std::max(settings.ClusterFar, camera.Near * 2.0f);
				grid.MaxLightsPerCluster = settings.MaxLightsPerCluster;
				// Per-cluster bitmasks over every local light (never truncated): the masks
				// address the frame's local lights, rounded up so the size changes rarely.
				grid.LightCapacity = std::max(32u, (lightResources.LocalCount + 255u) / 256u * 256u);
				R::ClusterView clusterView;
				clusterView.View = camera.View;
				clusterView.Projection = camera.Projection;
				const auto clusterResources = I.clusters->Record(graph, lightResources, grid, clusterView);

				lap("Build: Visibility and clusters");
				// --- Clustered Forward+ -------------------------------------------------
				R::ForwardPlusFrame forwardFrame;
				forwardFrame.Scene = &sceneResources;
				forwardFrame.Geometry = &geometryResources;
				forwardFrame.Visibility = &visible;
				forwardFrame.PageSlots = forwardSlots;
				forwardFrame.Materials = &materialResources;
				forwardFrame.Bindless = &I.bindless->GetTable();
				forwardFrame.Lights = &lightResources;
				forwardFrame.Clusters = &clusterResources;
				forwardFrame.Environment = environment ? &*environment : nullptr;
				forwardFrame.BrdfLut = lut;
				forwardFrame.Shadows = shadowResources ? &*shadowResources : nullptr;
				forwardFrame.View.ViewProjection = viewProjection;
				forwardFrame.View.PreviousViewProjection = I.previousViewProjection;
				forwardFrame.View.Jitter = jitter;
				forwardFrame.View.CameraPosition = camera.Position;
				forwardFrame.View.CameraForward = camera.Forward;
				forwardFrame.View.Ambient = settings.Ambient;
				forwardFrame.View.EnvironmentIntensity = settings.EnvironmentIntensity;
				forwardFrame.View.EnvironmentRotation = settings.EnvironmentRotation;
				forwardFrame.View.DebugMode = settings.Debug;
				forwardFrame.Transparent = settings.Transparent;
				forwardFrame.DeferLocalLights = settings.DeferredLocalLights;
				forwardResources = I.forward->Record(graph, forwardFrame, targets);

				lap("Build: Forward+");
				// --- Reflection probes (the local layer of the reflection hierarchy) --------
				// A few cube faces per frame (the scheduler's budget) are rendered like the main
				// view, from the probe, into small targets (lit with the global environment, so
				// probes never see each other: no recursion), resolved into the probe atlas with
				// their distances, and the probes that changed are prefiltered.
				const auto& probeSettings = settings.ReflectionProbes;

				if (I.probeRenderer && probeSettings.Enabled && !input.ReflectionProbes.empty())
				{
					if (I.probeRenderer->Ensure(probeSettings.Resolution, probeSettings.MaxProbes))
					{
						I.probeScheduler.Reset();
						I.pendingFilters.clear();
						I.filteredKey.assign(I.probeRenderer->GetMaxProbes(), ~std::uint64_t(0));
					}

					const auto plan = I.probeScheduler.Update(
						input.ReflectionProbes, camera.Position, I.frameIndex + 1, I.featureTime, probeSettings, input.ReflectionMovers);
					const auto atlases = I.probeRenderer->Import(graph);
					const std::uint32_t size = I.probeRenderer->GetResolution();

					for (const auto& capture : plan.Captures)
					{
						const auto& probe = input.ReflectionProbes[capture.Probe];
						const float nearPlane = std::max(probe.CaptureNear, 0.01f);
						const auto faceView = R::ReflectionProbes::CubeFaceView(capture.Face, probe.Position);
						const auto faceProjection = R::ReflectionProbes::CubeFaceProjection(nearPlane);
						const auto faceViewProjection = R::MultiplyRowMajor(faceProjection, faceView);
						const auto basis = R::ReflectionProbes::CubeFace(capture.Face);
						auto faceTargets = makeTargets(size, size, "Probe ");
						// The view's right is the cube's mirrored axis (CubeFaceView).
						recordSky(faceTargets, { -basis.Right[0], -basis.Right[1], -basis.Right[2] }, basis.Up, basis.Forward, size, size,
							"Probe sky");

						R::RenderViewDesc faceViewDesc;
						faceViewDesc.ViewProjection = faceViewProjection;
						faceViewDesc.CameraPosition = probe.Position;
						faceViewDesc.LodScale = float(size) * 0.5f;
						R::VisibilityFrameDesc faceVisibility;
						faceVisibility.View = R::BuildGpuViewRecord(faceViewDesc);
						faceVisibility.View.ExcludedObjectId = probe.OwnerObjectId; // The owner does not see itself.
						faceVisibility.IndexPages = indexPages;
						faceVisibility.ReadStats = false;
						faceVisibility.ZeroUnusedCommands = R::NeedsZeroedCommands(I.drawPath);
						const auto faceVisible = I.probeVisibility->Record(graph, sceneResources, geometryResources, faceVisibility);

						R::ClusterGridDesc faceGrid = grid;
						faceGrid.ViewportWidth = size;
						faceGrid.ViewportHeight = size;
						faceGrid.Near = nearPlane;
						faceGrid.Far = std::max(settings.ClusterFar, nearPlane * 2.0f);
						R::ClusterView faceClusterView;
						faceClusterView.View = faceView;
						faceClusterView.Projection = faceProjection;
						const auto faceClusters = I.clusters->Record(graph, lightResources, faceGrid, faceClusterView);

						R::ForwardPlusFrame faceFrame = forwardFrame;
						faceFrame.Visibility = &faceVisible;
						faceFrame.Clusters = &faceClusters;
						faceFrame.View.ViewProjection = faceViewProjection;
						faceFrame.View.PreviousViewProjection = faceViewProjection;
						faceFrame.View.Jitter = { 0.0f, 0.0f };
						faceFrame.View.CameraPosition = probe.Position;
						faceFrame.View.CameraForward = basis.Forward;
						faceFrame.View.DebugMode = R::ForwardPlusDebugMode::None;
						faceFrame.Transparent = false; // Glass is not worth a sort per captured face.
						faceFrame.DebugName = "Probe Forward+"; // Its own rows in the GPU timings.
						I.forward->Record(graph, faceFrame, faceTargets);
						R::ReflectionProbeRenderer::CaptureSky captureSky;

						if (environment && settings.SkyBackground)
						{
							captureSky.Environment = environment->Prefiltered;
							captureSky.Scale = settings.EnvironmentIntensity;
							captureSky.Rotation = settings.EnvironmentRotation;
						}

						I.probeRenderer->RecordResolve(
							graph, atlases, capture.Slot, capture.Face, faceTargets.Color, faceTargets.Depth, nearPlane, captureSky);
					}

					// Prefilter a bounded number of probes a frame (each is 6 faces x every mip):
					// probes not yet filtered for their current occupant first, then the ones
					// whose faces changed longest ago.
					I.filteredKey.resize(I.probeRenderer->GetMaxProbes(), ~std::uint64_t(0));

					for (const auto slot : plan.Filter)
					{
						if (std::find(I.pendingFilters.begin(), I.pendingFilters.end(), slot) == I.pendingFilters.end())
						{
							I.pendingFilters.push_back(slot);
						}
					}

					const auto keyOf = [&](std::uint32_t slot) -> std::uint64_t
					{
						for (const auto& active : plan.Active)
						{
							if (active.Slot == slot)
							{
								return input.ReflectionProbes[active.Probe].Key;
							}
						}

						return ~std::uint64_t(0);
					};
					const auto slotUnfiltered = [&](std::uint32_t slot, std::uint64_t key)
					{
						return slot < I.filteredKey.size() && I.filteredKey[slot] != key;
					};
					std::stable_sort(I.pendingFilters.begin(), I.pendingFilters.end(),
						[&](std::uint32_t a, std::uint32_t b)
						{
							const bool urgentA = slotUnfiltered(a, keyOf(a));
							const bool urgentB = slotUnfiltered(b, keyOf(b));
							return urgentA && !urgentB;
						});
					const std::uint32_t budget = std::max(probeSettings.FiltersPerFrame, 1u);
					std::uint32_t filtered = 0;

					while (!I.pendingFilters.empty() && filtered < budget)
					{
						const std::uint32_t slot = I.pendingFilters.front();
						I.pendingFilters.erase(I.pendingFilters.begin());

						if (slot >= I.filteredKey.size())
						{
							continue;
						}

						I.probeRenderer->RecordFilter(graph, atlases, slot, probeSettings.PrefilterSamples);
						I.filteredKey[slot] = keyOf(slot);
						++filtered;
					}

					graph.Export(atlases.Source, ResourceState::ShaderRead);
					graph.Export(atlases.Prefiltered, ResourceState::ShaderRead);
					stats.ReflectionProbeFaces = static_cast<std::uint32_t>(plan.Captures.size());

					if (!plan.Active.empty())
					{
						std::vector<R::GpuReflectionProbeRecord> records;

						for (const auto& active : plan.Active)
						{
							const auto& probe = input.ReflectionProbes[active.Probe];

							if (active.Slot >= I.filteredKey.size() || I.filteredKey[active.Slot] != probe.Key)
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

						R::ScreenSpaceFrame::ProbeInputs inputs;
						inputs.Cubes = atlases.Prefiltered;
						inputs.Records = graph.CreateUpload(std::as_bytes(std::span(records)), "Reflection probe records", S::BufferUsage::Storage, 16);
						inputs.ObjectId = targets.ObjectId;
						inputs.Count = static_cast<std::uint32_t>(records.size());
						inputs.MipCount = I.probeRenderer->GetMipCount();
						probeInputs = inputs;
						stats.ReflectionProbes = inputs.Count;
					}
				}

				// --- Planar reflections (the sharp top of the reflection hierarchy) ----------
				// A few captures per frame (the planner's budget), each a small Forward+ render from
				// the camera mirrored in the reflector, through it as a window: the off-axis frustum
				// spans only the reflector's visible portal and its near plane is the mirror, so GPU
				// visibility culls everything else (and the reflector itself) at coarse LODs. The
				// captures land in a persistent atlas the composite reprojects until they are re-rendered.
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

						for (const auto& capture : plan.Captures)
						{
							auto captureTargets = makeTargets(capture.Width, capture.Height, "Planar ");
							// Sky rays of the off-axis view: forward shifted to the window's centre.
							const auto& f = capture.FrustumScale;
							std::array<float, 3> rayRight{}, rayUp{}, rayForward{};

							for (int c = 0; c < 3; ++c)
							{
								rayRight[c] = capture.Right[c] / f[0];
								rayUp[c] = capture.Up[c] / f[1];
								rayForward[c] = capture.Forward[c] + capture.Right[c] * (f[2] / f[0]) + capture.Up[c] * (f[3] / f[1]);
							}

							recordSky(captureTargets, rayRight, rayUp, rayForward, capture.Width, capture.Height, "Planar sky");

							R::RenderViewDesc captureViewDesc;
							captureViewDesc.ViewProjection = capture.ViewProjection;
							captureViewDesc.CameraPosition = capture.Position;
							captureViewDesc.LodScale = capture.LodScale;
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
							captureGrid.ViewportWidth = capture.Width;
							captureGrid.ViewportHeight = capture.Height;
							captureGrid.Near = capture.NearClip;
							captureGrid.Far = std::max(std::min(settings.ClusterFar, planarSettings.CullDistance), capture.NearClip * 2.0f);
							R::ClusterView captureClusterView;
							captureClusterView.View = capture.View;
							captureClusterView.Projection = capture.Projection;
							const auto captureClusters = I.clusters->Record(graph, lightResources, captureGrid, captureClusterView);

							R::ForwardPlusFrame captureFrame = forwardFrame;
							captureFrame.Visibility = &captureVisible;
							captureFrame.Clusters = &captureClusters;
							captureFrame.View.ViewProjection = capture.ViewProjection;
							captureFrame.View.PreviousViewProjection = capture.ViewProjection;
							captureFrame.View.Jitter = { 0.0f, 0.0f };
							captureFrame.View.CameraPosition = capture.Position;
							captureFrame.View.CameraForward = capture.Forward;
							captureFrame.View.DebugMode = R::ForwardPlusDebugMode::None;
							captureFrame.Transparent = false;		  // No sort per capture.
							captureFrame.DebugName = "Planar Forward+"; // Its own rows in the GPU timings.
							I.forward->Record(graph, captureFrame, captureTargets);
							R::ReflectionProbeCaptureSky captureSky;

							if (environment && settings.SkyBackground)
							{
								captureSky.Environment = environment->Prefiltered;
								captureSky.Scale = settings.EnvironmentIntensity;
								captureSky.Rotation = settings.EnvironmentRotation;
							}

							I.planarRenderer->RecordResolve(graph, atlas, capture, captureTargets.Color, captureTargets.Depth, captureSky);
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

			lap("Build: Reflection probes");
			// --- Particles (simulation; drawn after the composite, below) ---------------
			stats.ParticleEmitters = I.particles->GetStats().LiveEmitters;
			std::optional<R::ParticleGraphResources> simulatedParticles;

			if (settings.Particles && stats.ParticleEmitters > 0)
			{
				R::ParticleView particleView;
				particleView.View = camera.View;
				particleView.Projection = camera.Projection;
				particleView.Jitter = jitter;
				const float dt = std::clamp(std::isfinite(input.SimulationDeltaTime) ? input.SimulationDeltaTime : 0.0f, 0.0f, 0.1f);
				simulatedParticles = I.particles->Simulate(graph, particleView, dt);
				particlesPending = true;
			}

			lap("Build: Particles");
			// --- Screen-space effects, TAA and post -------------------------------------
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

			ssFrame.NoiseFrame = static_cast<std::uint32_t>(I.frameIndex);
			ssFrame.BackDepth = targets.BackDepth;
			ssFrame.Probes = probeInputs;
			ssFrame.Planar = planarInputs;
			// Reflections of reflections: rays read the previous resolved frame (which holds
			// its reflections) at the hit's reprojected position. Only with TAA, whose
			// history it is, and Record below reuses the same import.
			if (temporalOn && ssFrame.Settings.Reflections.Enabled && ssFrame.Settings.Reflections.History && targets.Velocity)
			{
				ssFrame.History = I.temporal->ImportPreviousOutput(graph, width, height);

				if (ssFrame.History)
				{
					ssFrame.Velocity = *targets.Velocity;
				}
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

			const auto screen = I.screenSpace->Record(graph, ssFrame);

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

			// Render features (gameplay-added passes) run at three stages of the frame.
			const auto runFeatures = [&](RenderFeatureStage stage, R::GraphTexture color)
			{
				std::vector<RenderFeature*> ordered;

				for (const auto& feature : features)
				{
					if (feature && feature->Enabled && feature->GetStage() == stage)
					{
						ordered.push_back(feature.get());
					}
				}

				if (ordered.empty() || !draw3D)
				{
					return color;
				}

				std::stable_sort(ordered.begin(), ordered.end(),
					[](const RenderFeature* a, const RenderFeature* b)
					{
						return a->GetOrder() < b->GetOrder();
					});
				RenderFeatureContext context(graph, stage, featureView, settings, color, targets.Depth, featureServices());

				for (auto* feature : ordered)
				{
					// A broken feature (missing program, wrong bindings) is switched off with a
					// log line instead of failing every frame.
					try
					{
						feature->Record(context);
					}
					catch (const std::exception& error)
					{
						feature->Enabled = false;
						std::cerr << "[Render] Feature '" << feature->GetName() << "' disabled: " << error.what() << '\n';
					}
				}

				return context.Color();
			};

			R::GraphTexture sceneColor = runFeatures(RenderFeatureStage::BeforeTemporal, screen.Output);
			// Particles blend over the finished scene: drawn into the Forward+ color, the
			// screen-space composite re-fogged them with the depth behind them and the clouds
			// were composited over them wherever the sky showed through, so near the horizon
			// (thick fog, long cloud paths) whole bands of a fountain vanished. They still
			// depth-test against the scene and are resolved by TAA like everything else.
			if (simulatedParticles)
			{
				const R::ParticleRenderProgram program{ I.particleAdditive.get(), I.particleAlpha.get(), I.particleRender.Layout.get() };
				I.particles->Draw(graph, *simulatedParticles, program, { sceneColor, targets.Depth }, I.bindless->GetTable());
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

			resolved = runFeatures(RenderFeatureStage::BeforePostProcess, resolved);

			auto postOutput = target(S::Format::RGBA8Unorm,
				S::TextureUsage::Storage | S::TextureUsage::ColorAttachment | S::TextureUsage::Sampled | S::TextureUsage::TransferSource,
				"Frame");
			R::PostProcessFrame postFrame;
			postFrame.Source = resolved;
			postFrame.Output = postOutput;
			postFrame.Settings = settings.Post;
			postFrame.Settings.Output.Encoding = R::OutputEncoding::Srgb;
			postFrame.DeltaTime = std::clamp(std::isfinite(input.DeltaTime) ? input.DeltaTime : 0.0f, 0.0f, 1.0f);
			I.post->Record(graph, postFrame);
			{
				// Display-referred features write their own RGBA8 target; the UI and the
				// presentation then use it (it keeps the Frame target's usages).
				const auto finalColor = runFeatures(RenderFeatureStage::AfterPostProcess, postOutput);

				if (finalColor != postOutput)
				{
					postOutput = finalColor;
				}
			}

			lap("Build: Screen space, TAA, features and post");
			// --- UI -----------------------------------------------------------------------
			stats.UiQuads = 0;

			if (settings.Ui && !input.Ui.empty() && input.GlyphAtlas)
			{
				if (I.attachedAtlas && I.attachedAtlas != input.GlyphAtlas)
				{
					I.atlasTextures->Release(lastCompletion);
				}

				I.attachedAtlas = input.GlyphAtlas;
				const auto atlasFrame = I.atlasTextures->Update(graph, *input.GlyphAtlas);
				atlasPending = true;
				const R::UiRenderProgram program{ I.uiPipeline.get(), I.uiQuad.Layout.get(), I.uiDepthPipeline.get() };

				for (const auto& item : input.Ui)
				{
					if (!item.Document || item.Opacity <= 0.0f)
					{
						continue;
					}

					auto& document = *item.Document;
					document.EnsureLayout();
					R::UiRenderFrame uiFrame;
					uiFrame.Paint = document.Paint(*input.GlyphAtlas);
					uiFrame.Target = postOutput;
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
					uiFrame.Composition.Encoding = R::UiOutputEncoding::Srgb;

					if (I.ui->Record(graph, uiFrame, program, I.bindless->GetTable()))
					{
						stats.UiQuads += I.ui->GetStats().Quads;
					}
				}
			}

			lap("Build: UI");
			// --- Capture and presentation -------------------------------------------------
			if (input.Capture)
			{
				captureReadback = R::AddTextureReadback(graph, "Frame capture", postOutput, { 0, {}, {}, { width, height, 1 } });
			}

			if (frame.Target)
			{
				const auto surface =
					graph.ImportTexture(*frame.Target, frame.Presented ? ResourceState::Present : ResourceState::Undefined);
				const auto format = device.GetSwapchainFormat();
				auto* pipeline = &I.GetPresentPipeline(format);
				auto* layout = I.present.Layout.get();
				auto* sampler = I.presentSampler.get();
				const PresentConstants constants{ IsSrgbFormat(format) ? 1u : 0u, {} };
				graph.AddPass(
					"Present", S::QueueType::Graphics,
					[&](R::RenderGraphBuilder& b)
					{
						b.Read(postOutput, ResourceState::ShaderRead);
						b.Write(surface, ResourceState::ColorAttachment);
					},
					[postOutput, surface, pipeline, layout, sampler, constants, width, height](R::RenderCommandContext& c)
					{
						auto& source = c.CreateView(postOutput);
						auto& targetView = c.CreateView(surface);
						auto table = c.Device().CreateDescriptorTable({ layout, 0, 0, "Present source" });

						if (!table)
						{
							throw std::runtime_error("FrameRenderer: cannot create the present descriptor table");
						}

						std::array<S::DescriptorWrite, 2> writes{};
						writes[0].Binding = 0;
						writes[0].TextureResource = &source;
						writes[1].Binding = 1;
						writes[1].SamplerResource = sampler;
						table->Write(writes);
						auto& retained = static_cast<S::DescriptorTable&>(c.Retain(std::move(table)));
						S::RenderingAttachmentDesc attachment{};
						attachment.View = &targetView;
						attachment.Load = S::LoadOp::Discard;
						auto& list = c.Commands();
						const std::uint32_t w = width;
						const std::uint32_t h = height;
						list.BeginRendering({ { &attachment, 1 }, nullptr, { w, h } });
						list.BindGraphicsPipeline(*pipeline);
						list.BindDescriptorTable(0, retained);
						list.SetViewport({ 0, 0, float(w), float(h) });
						list.SetScissor({ 0, 0, w, h });
						list.PushConstants(
							S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));
						list.Draw(3);
						list.EndRendering();
					});
				graph.Export(surface, ResourceState::Present);
			}
			else
			{
				// Headless: keep the frame alive to the end of the graph.
				graph.Export(postOutput, ResourceState::ShaderRead);
			}

			lap("Build: capture and presentation");
			const auto compiled = graph.Compile();
			lap("Graph compile");
			completion = executor.Execute(compiled, device.GetSubmit(frame));
			lap("Record and submit");
			{
				const auto& t = executor.GetLastExecuteTimings();
				stats.RecordPhases = { { "Executor: wait", t.Wait }, { "Executor: allocate transients", t.Allocate },
					{ "Executor: stage uploads", t.Stage }, { "Executor: query pool", t.Queries }, { "Executor: record passes", t.Record },
					{ "Executor: submit", t.Submit } };
				stats.RecordPasses.clear();

				for (const auto& pass : t.Passes)
				{
					stats.RecordPasses.push_back({ pass.Name, pass.Nanoseconds.value_or(0.0) * 1.0e-6 });
				}
			}
		}
		catch (...)
		{
			if (residencyImported)
			{
				I.residency->AbortUploads();
			}

			if (materialsImported)
			{
				I.materialTable->AbortUploads();
			}

			if (sceneImported)
			{
				I.scene->AbortUploads();
			}

			if (lightsImported)
			{
				I.lights->AbortUploads();
			}

			if (particlesPending)
			{
				I.particles->AbortFrame();
			}

			if (skinningPending)
			{
				I.skinning->AbortFrame();
			}

			if (atlasPending)
			{
				I.atlasTextures->AbortFrame();
			}

			throw;
		}

		lastCompletion = completion;
		device.SetLastCompletion(completion);
		I.residency->CommitUploads(completion);
		I.materialTable->CommitUploads();
		I.scene->CommitUploads();
		I.lights->CommitUploads();

		if (particlesPending)
		{
			I.particles->CommitFrame();
		}

		if (skinningPending)
		{
			I.skinning->CommitFrame();
		}

		if (atlasPending)
		{
			I.atlasTextures->CommitFrame();
		}

		I.whiteUploaded = true;
		I.lutBuilt = true;

		if (settings.Environment)
		{
			I.builtSky = settings.Sky;
			I.environmentValid = true;

			if (environmentRebuilt)
			{
				I.environmentBuiltTime = I.featureTime;
				I.environmentHadFeatures = environmentWithFeatures;
			}
		}

		I.previousViewProjection = viewProjection;
		++I.frameIndex;

		lap("Commit uploads");
		stats.Presented = device.Present(frame);
		lap("Present");

		if (captureReadback)
		{
			executor.Wait();
			capture.resize(std::size_t(width) * height * 4);

			if (executor.TryReadback(captureReadback->Buffer, std::as_writable_bytes(std::span(capture))) == S::ReadbackStatus::Ready)
			{
				captureWidth = width;
				captureHeight = height;
			}
			else
			{
				capture.clear();
			}
		}

		const auto sceneStats = I.scene->GetStats();
		const auto lightStats = I.lights->GetStats();
		stats.Frame = I.frameIndex;
		stats.Width = width;
		stats.Height = height;
		stats.RenderObjects = sceneStats.LiveObjects;
		stats.Materials = I.materials->GetCount();
		stats.DirectionalLights = lightStats.DirectionalLights;
		stats.LocalLights = lightStats.LocalLights;
		stats.SkinnedInstances = I.skinning->GetStats().Instances;
		stats.ResidentMeshes = I.meshes->GetResidentMeshCount();
		stats.ResidentTextures = I.meshes->GetResidentTextureCount();
		stats.PendingAssets =
			(I.meshes->GetRequestedMeshCount() - stats.ResidentMeshes) + (I.meshes->GetRequestedTextureCount() - stats.ResidentTextures);
		stats.CpuMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - cpuStart).count();
		(void)pageConflict;
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

	bool FrameRenderer::WriteCapture(const std::filesystem::path& path) const
	{
		if (capture.empty() || !captureWidth || !captureHeight)
		{
			return false;
		}

		if (path.has_parent_path())
		{
			std::error_code ignored;
			std::filesystem::create_directories(path.parent_path(), ignored);
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);

		if (!file)
		{
			return false;
		}

		file << "P6\n" << captureWidth << ' ' << captureHeight << "\n255\n";
		std::vector<char> row(std::size_t(captureWidth) * 3);

		for (std::uint32_t y = 0; y < captureHeight; ++y)
		{
			for (std::uint32_t x = 0; x < captureWidth; ++x)
			{
				const std::size_t source = (std::size_t(y) * captureWidth + x) * 4;
				row[x * 3] = static_cast<char>(capture[source]);
				row[x * 3 + 1] = static_cast<char>(capture[source + 1]);
				row[x * 3 + 2] = static_cast<char>(capture[source + 2]);
			}

			file.write(row.data(), static_cast<std::streamsize>(row.size()));
		}

		return static_cast<bool>(file);
	}

} // namespace Engine
