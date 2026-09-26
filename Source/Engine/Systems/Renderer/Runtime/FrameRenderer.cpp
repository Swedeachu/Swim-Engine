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
		for (auto& a : Ambient)
		{
			clampFinite(a, 0.0f, 100.0f, 0.0f);
		}
		clampFinite(EnvironmentIntensity, 0.0f, 100.0f, 1.0f);
		clampFinite(EnvironmentRotation, -100.0f, 100.0f, 0.0f);
		EnvironmentResolution = std::clamp(PowerOfTwoFloor(std::max(EnvironmentResolution, 16u)), 16u, 1024u);
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
		RuntimeComputeProgram postHistogram, postExposure, postBloomDown, postBloomUp, postComposite, postCompositeHdr;
		RuntimeComputeProgram temporalResolve, ssAo, ssBlur, ssComposite, ssReflection;
		RuntimeComputeProgram particleSimulate, particleEmit, particleCompact, particleFinalize, skinningProgram;
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
		std::unique_ptr<R::ScreenSpaceEffects> screenSpace;
		std::unique_ptr<R::TemporalAntiAliasing> temporal;
		std::unique_ptr<R::PostProcessor> post;
		std::unique_ptr<R::UiRenderer> ui;
		std::unique_ptr<R::UiAtlasTextures> atlasTextures;
		const Swim::Text::GlyphAtlas* attachedAtlas = nullptr;

		// Visibility instances (rebuilt when the page-slot count changes).
		std::unique_ptr<R::GpuVisibility> visibility;
		std::unique_ptr<R::GpuVisibility> shadowVisibility;
		std::uint32_t visibilitySlots = 0;
		std::map<std::uint32_t, R::StandardPbr::Parameters> routes; // Material set -> parameters.

		// Shadows.
		std::unique_ptr<R::ShadowAtlasAllocator> atlas;
		std::uint32_t atlasSize = 0;
		std::uint32_t atlasMinTile = 0;

		// Environment (persistent, rebuilt when the sky changes).
		std::unique_ptr<S::Texture> prefiltered;
		std::unique_ptr<S::Buffer> irradiance;
		std::unique_ptr<S::Texture> brdfLut;
		R::EnvironmentMapDesc environmentMap;
		std::optional<R::Environment::ProceduralSky> builtSky;
		bool lutBuilt = false;
		bool environmentValid = false;
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
			// The sky pass writes the seven Forward+ targets (so it can clear them) and clears depth.
			static const std::array<S::Format, 7> formats{ R::ForwardPlusRenderer::ColorFormat, R::ForwardPlusRenderer::ObjectIdFormat,
				R::ForwardPlusRenderer::VelocityFormat, R::ForwardPlusRenderer::NormalFormat, R::ForwardPlusRenderer::IndirectFormat,
				R::ForwardPlusRenderer::ReflectanceFormat, R::ForwardPlusRenderer::SpecularFormat };
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
		forwardDesc.SortPipeline = sortProgram.Pipeline.get();
		forwardDesc.SortLayout = sortProgram.Layout.get();
		forwardDesc.DrawPath = drawPath;
		forward = std::make_unique<R::ForwardPlusRenderer>(device, forwardDesc);

		R::ShadowRendererDesc shadowDesc;
		shadowDesc.Opaque = { shadowDepthPipeline.get(), shadowDepth.Layout.get() };
		shadowDesc.Masked = { shadowMaskedPipeline.get(), shadowMasked.Layout.get() };
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
		environmentBuilder = std::make_unique<R::EnvironmentBuilder>(environmentDesc);

		R::ScreenSpaceEffectsDesc ssDesc;
		ssDesc.AmbientOcclusion = { ssAo.Pipeline.get(), ssAo.Layout.get(), ssAo.Space };
		ssDesc.Blur = { ssBlur.Pipeline.get(), ssBlur.Layout.get(), ssBlur.Space };
		ssDesc.Composite = { ssComposite.Pipeline.get(), ssComposite.Layout.get(), ssComposite.Space };
		ssDesc.Reflection = { ssReflection.Pipeline.get(), ssReflection.Layout.get(), ssReflection.Space };
		screenSpace = std::make_unique<R::ScreenSpaceEffects>(ssDesc);

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
		visibilityDesc.MaterialBinCapacities = R::ShadowRenderer::VisibilityBinCapacities(desc.MaxObjects, 4096);
		visibilityDesc.DebugName = "Shadow visibility";
		shadowVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilitySlots = slots;
		for (const auto& [set, parameters] : routes)
		{
			R::ForwardPlusRenderer::RouteMaterial(*visibility, set, parameters);
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
		std::vector<RenderStats::PassTiming> passes;
		double total = 0.0;
		for (const auto& timing : timings)
		{
			if (timing.Nanoseconds)
			{
				const double ms = *timing.Nanoseconds * 1.0e-6;
				total += ms;
				passes.push_back({ timing.Name, ms });
			}
		}
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
		auto& executor = device.GetExecutor();
		settings.Sanitize();
		// The previous frame must be complete before its executor, staging and acquire
		// semaphore are reused (one submission in flight); the CPU work since BeginFrame
		// overlapped it.
		GatherTimings();

		auto frame = device.Acquire();
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
				const bool rebuild = !I.builtSky || !SameSky(*I.builtSky, settings.Sky) || !I.environmentValid;
				R::EnvironmentTargets targets;
				targets.Prefiltered = graph.ImportTexture(*I.prefiltered, rebuild ? ResourceState::Undefined : ResourceState::ShaderRead);
				targets.Irradiance = graph.ImportBuffer(*I.irradiance, rebuild ? ResourceState::Undefined : ResourceState::ShaderRead);
				if (rebuild)
				{
					environment = I.environmentBuilder->Record(graph, settings.Sky, I.environmentMap, targets);
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

			// --- Frame targets ----------------------------------------------------------
			const auto target = [&](S::Format format, S::TextureUsage usage, const char* name)
			{
				S::TextureDesc textureDesc;
				textureDesc.Extent = { width, height, 1 };
				textureDesc.PixelFormat = format;
				textureDesc.Usage = usage;
				textureDesc.DebugName = name;
				return graph.CreateTexture(textureDesc);
			};
			const auto attachmentSampled = S::TextureUsage::ColorAttachment | S::TextureUsage::Sampled;
			R::ForwardPlusTargets targets;
			targets.Color = target(R::ForwardPlusRenderer::ColorFormat, attachmentSampled | S::TextureUsage::TransferSource, "Scene color");
			targets.ObjectId =
				target(R::ForwardPlusRenderer::ObjectIdFormat, attachmentSampled | S::TextureUsage::TransferSource, "Object id");
			targets.Depth =
				target(R::CanonicalDepthFormat, S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled, "Scene depth");
			targets.Velocity = target(R::ForwardPlusRenderer::VelocityFormat, attachmentSampled, "Velocity");
			targets.Normal = target(R::ForwardPlusRenderer::NormalFormat, attachmentSampled, "Normal");
			targets.Indirect = target(R::ForwardPlusRenderer::IndirectFormat, attachmentSampled, "Indirect");
			targets.Reflectance = target(R::ForwardPlusRenderer::ReflectanceFormat, attachmentSampled, "Reflectance");
			targets.Specular = target(R::ForwardPlusRenderer::SpecularFormat, attachmentSampled, "Specular");
			targets.Clear = false; // The sky pass clears them.

			// --- Sky background (clears every Forward+ target) ---------------------------
			{
				SkyConstants sky{};
				const float tanY = std::tan(camera.VerticalFov * 0.5f);
				const float tanX = tanY * camera.Aspect;
				const bool drawSky = settings.SkyBackground;
				const auto& s = settings.Sky;
				const auto sun = Normalized(s.SunDirection);
				for (int c = 0; c < 3; ++c)
				{
					sky.RayRight[c] = camera.Right[c] * tanX;
					sky.RayUp[c] = camera.Up[c] * tanY;
					sky.RayForward[c] = camera.Forward[c];
					sky.Zenith[c] = drawSky ? s.ZenithColor[c] : settings.ClearColor[c];
					sky.Horizon[c] = drawSky ? s.HorizonColor[c] : settings.ClearColor[c];
					sky.Ground[c] = drawSky ? s.GroundColor[c] : settings.ClearColor[c];
					sky.SunDirection[c] = sun[c];
					sky.SunColor[c] = drawSky ? s.SunColor[c] : 0.0f;
				}
				sky.RayRight[3] = drawSky ? s.Intensity * settings.EnvironmentIntensity : 1.0f;
				sky.RayUp[3] = settings.EnvironmentRotation;
				sky.RayForward[3] = std::max(s.SunSharpness, 1.0f);
				auto* pipeline = I.skyPipeline.get();
				auto* layout = I.skyBackground.Layout.get();
				(void)layout;
				graph.AddPass(
					"Sky background", S::QueueType::Graphics,
					[&](R::RenderGraphBuilder& b)
					{
						b.Write(targets.Color, ResourceState::ColorAttachment);
						b.Write(targets.ObjectId, ResourceState::ColorAttachment);
						b.Write(*targets.Velocity, ResourceState::ColorAttachment);
						b.Write(*targets.Normal, ResourceState::ColorAttachment);
						b.Write(*targets.Indirect, ResourceState::ColorAttachment);
						b.Write(*targets.Reflectance, ResourceState::ColorAttachment);
						b.Write(*targets.Specular, ResourceState::ColorAttachment);
						b.Write(targets.Depth, ResourceState::DepthStencilWrite);
					},
					[targets, sky, pipeline, width, height](R::RenderCommandContext& c)
					{
						std::array<S::RenderingAttachmentDesc, 7> colors{};
						const std::array<R::GraphTexture, 7> textures{ targets.Color, targets.ObjectId, *targets.Velocity, *targets.Normal,
							*targets.Indirect, *targets.Reflectance, *targets.Specular };
						for (std::size_t i = 0; i < colors.size(); ++i)
						{
							colors[i].View = &c.CreateView(textures[i]);
							colors[i].Load = S::LoadOp::Clear;
							colors[i].Clear.Value = { 0.0f, 0.0f, 0.0f, 0.0f };
						}
						S::TextureViewDesc depthView;
						depthView.PixelFormat = R::CanonicalDepthFormat;
						const S::DepthStencilAttachmentDesc depth{ &c.CreateView(targets.Depth, depthView), S::LoadOp::Clear,
							S::StoreOp::Store, R::DepthClearValue(R::CanonicalDepthConvention), 0 };
						auto& list = c.Commands();
						list.BeginRendering({ colors, &depth, { width, height } });
						list.BindGraphicsPipeline(*pipeline);
						list.SetViewport({ 0, 0, float(width), float(height) });
						list.SetScissor({ 0, 0, width, height });
						list.PushConstants(S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&sky, 1)));
						list.Draw(3);
						list.EndRendering();
					});
			}

			R::RenderViewDesc viewDesc;
			viewDesc.ViewProjection = viewProjection;
			viewDesc.CameraPosition = camera.Position;

			stats.ShadowViews = 0;
			stats.ShadowCasters = static_cast<std::uint32_t>(input.ShadowCasters.size());
			stats.Rendered3D = draw3D;
			stats.PageSlots = static_cast<std::uint32_t>(pageSlots.size());
			std::optional<R::ForwardPlusGraphResources> forwardResources;
			if (draw3D)
			{
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
						shadowResources = I.shadows->Record(graph, shadowFrame);
						stats.ShadowViews = plan->Stats.Views;
					}
				}

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
				forwardResources = I.forward->Record(graph, forwardFrame, targets);
			}

			// --- Particles --------------------------------------------------------------
			stats.ParticleEmitters = I.particles->GetStats().LiveEmitters;
			if (settings.Particles && stats.ParticleEmitters > 0)
			{
				R::ParticleView particleView;
				particleView.View = camera.View;
				particleView.Projection = camera.Projection;
				particleView.Jitter = jitter;
				const float dt = std::clamp(std::isfinite(input.SimulationDeltaTime) ? input.SimulationDeltaTime : 0.0f, 0.0f, 0.1f);
				const auto simulated = I.particles->Simulate(graph, particleView, dt);
				particlesPending = true;
				const R::ParticleRenderProgram program{ I.particleAdditive.get(), I.particleAlpha.get(), I.particleRender.Layout.get() };
				I.particles->Draw(graph, simulated, program, { targets.Color, targets.Depth }, I.bindless->GetTable());
			}

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
			const auto screen = I.screenSpace->Record(graph, ssFrame);

			// Render features (gameplay-added passes) run at three stages of the frame.
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
				RenderFeatureContext context(graph, stage, featureView, settings, color, targets.Depth, std::move(services));
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

			const auto compiled = graph.Compile();
			completion = executor.Execute(compiled, device.GetSubmit(frame));
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
		}
		I.previousViewProjection = viewProjection;
		++I.frameIndex;

		stats.Presented = device.Present(frame);

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
