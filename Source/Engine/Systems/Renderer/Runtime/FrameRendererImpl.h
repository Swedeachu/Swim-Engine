#pragma once

// FrameRenderer's private state, shared by the FrameRenderer*.cpp files.

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
#include "Engine/Systems/Renderer/Runtime/SceneViewports.h"
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
#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <algorithm>
#include <utility>
#include <map>
#include <tuple>
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
#include <set>
#include <stdexcept>

namespace Engine
{

	namespace S = Swim::Rhi;
	namespace R = Swim::Render;

	namespace FrameRendererDetail
	{

		using S::ResourceState;

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

		// Draws source (the display-referred frame) into target, decoding sRGB for an sRGB target.
		void AddPresentPass(R::RenderGraph& graph, R::GraphTexture source, R::GraphTexture target, S::GraphicsPipeline& pipelineRef,
			S::PipelineLayout& layoutRef, S::Sampler& samplerRef, bool srgb, std::uint32_t width, std::uint32_t height);

		// FXAA or the morphological edges/weights/blend chain, then sharpening, on the
		// display-referred frame (RGBA8 sRGB-encoded, ScreenAntiAliasing.slang).
		R::GraphTexture RecordScreenAntiAliasing(R::RenderGraph& graph, const RenderFeatureView& view, const RenderSettings& settings,
			R::GraphTexture color, R::GraphTexture depth, std::optional<R::GraphTexture> normal, RenderFeatureContext::Services services);

		// Half-size levels the screen blur (RenderSettings::Blur) uses for a frame height, 0 when
		// off; offset (optional) receives the tap offset in half texels.
		std::uint32_t ScreenBlurLevels(const RenderSettings& settings, std::uint32_t height, float* offset = nullptr);

		// The dual-filter blur chain over the display-referred frame (ScreenBlur.slang), mixed
		// back by strength everywhere or inside RenderSettings::Blur.Regions; color when off.
		R::GraphTexture RecordScreenBlur(R::RenderGraph& graph, const RenderFeatureView& view, const RenderSettings& settings,
			R::GraphTexture color, RenderFeatureContext::Services services);

		// Throws when a creation returned null; what names it in the message.
		template <typename Pointer> Pointer Require(Pointer pointer, const char* what)
		{
			if (!pointer)
			{
				throw std::runtime_error(std::string("FrameRenderer: cannot create the ") + what + " pipeline");
			}

			return pointer;
		}

		bool IsSrgbFormat(S::Format format);
		std::array<float, 3> Normalized(std::array<float, 3> v);
		bool SameSky(const R::Environment::ProceduralSky& a, const R::Environment::ProceduralSky& b);

		// A transient texture named prefix + name.
		R::GraphTexture CreateTarget(R::RenderGraph& graph, std::uint32_t width, std::uint32_t height, const char* prefix, S::Format format,
			S::TextureUsage usage, const char* name);

		// A complete set of Forward+ targets (the main view, probe faces, planar captures).
		R::ForwardPlusTargets CreateForwardTargets(
			R::RenderGraph& graph, std::uint32_t targetWidth, std::uint32_t targetHeight, const char* prefix);

		// The multisampled Forward+ targets the sky and opaque passes draw into (no back depth).
		R::ForwardPlusTargets CreateMsaaTargets(
			R::RenderGraph& graph, std::uint32_t targetWidth, std::uint32_t targetHeight, S::SampleCount samples);

	} // namespace FrameRendererDetail

	struct FrameRenderer::Impl
	{
		Impl(RenderDevice& renderDevice, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io, Swim::Jobs::JobSystem* jobs,
			const FrameRendererDesc& desc);

		~Impl();

		// Construction, in order.
		void LoadComputePrograms();
		void CreateDrawPipelines();
		void CreateSharedResources();
		void CreateResidency(Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io, Swim::Jobs::JobSystem* jobs);
		void CreateScenePasses();
		void CreateEffects();

		S::Device& device;
		RenderDevice& renderDevice;
		ShaderLibrary shaders;
		R::VisibilityDrawPath drawPath;
		FrameRendererDesc desc;

		// Programs.
		RuntimeComputeProgram visibilityProgram, clusterCull, clusterBounds, clusterAssign, clusterScan, sortProgram;
		RuntimeComputeProgram environmentSky, environmentDownsample, environmentPrefilter, environmentIrradiance, environmentLut;
		RuntimeComputeProgram environmentOverlay;			// Optional: feature overlays (clouds) in the environment.
		RuntimeComputeProgram probeResolve, probePrefilter; // Optional: reflection probes.
		RuntimeComputeProgram planarResolve;				// Optional: planar reflections.
		RuntimeComputeProgram postHistogram, postExposure, postBloomDown, postBloomUp, postComposite, postCompositeHdr;
		RuntimeComputeProgram temporalResolve, ssAo, ssBlur, ssComposite, ssReflection, ssReflectionTemporal, ssLinearDepth,
			ssCompositeHalf;
		RuntimeComputeProgram particleSimulate, particleEmit, particleCompact, particleFinalize, skinningProgram;
		RuntimeGraphicsProgram forwardBackDepth; // Optional (screen-space reflection thickness).
		std::unique_ptr<S::GraphicsPipeline> forwardBackDepthPipeline;
		// Optional: deferred local lights (RenderSettings::DeferredLocalLights).
		RuntimeGraphicsProgram forwardDeferred;
		std::unique_ptr<S::GraphicsPipeline> forwardDeferredPipeline;
		RuntimeComputeProgram forwardLocalLights;
		RuntimeGraphicsProgram forwardOpaque, forwardTransparent, forwardDepth, forwardPrepassed, shadowDepth, shadowMasked, particleRender,
			uiQuad, sceneViewport, skyBackground, present;
		std::unique_ptr<S::GraphicsPipeline> forwardOpaquePipeline, forwardTransparentPipeline, forwardDepthPipeline,
			forwardPrepassedPipeline, shadowDepthPipeline, shadowMaskedPipeline;
		std::unique_ptr<S::GraphicsPipeline> particleAdditive, particleAlpha, uiPipeline, uiDepthPipeline, uiInvertPipeline,
			uiInvertDepthPipeline, skyPipeline, sceneViewportPipeline, sceneViewportTranslucentPipeline;
		std::map<S::Format, std::unique_ptr<S::GraphicsPipeline>> presentPipelines;

		// Shared resources.
		std::unique_ptr<S::Texture> whiteTexture;
		std::unique_ptr<S::TextureView> whiteView;
		std::unique_ptr<S::Sampler> linearRepeat, pointRepeat, linearClamp, presentSampler;
		std::unique_ptr<R::BindlessResourceTable> bindless;
		std::uint32_t materialSampler = 0;
		std::uint32_t pointMaterialSampler = 0;
		bool whiteUploaded = false;
		std::optional<R::BindlessTextureHandle> glintTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> glintAsset;
		std::chrono::steady_clock::time_point glintEpoch = std::chrono::steady_clock::now();

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
		std::unique_ptr<SceneViewports> viewports;
		const Swim::Text::GlyphAtlas* attachedAtlas = nullptr;

		// Visibility instances (rebuilt when the page-slot count changes).
		std::unique_ptr<R::GpuVisibility> visibility;
		std::unique_ptr<R::GpuVisibility> shadowVisibility;
		std::unique_ptr<R::GpuVisibility> probeVisibility;	// Reflection probe capture views (own LOD history).
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
		// The static layer (RenderSettings::ShadowStaticLayer) and what its depth was drawn from:
		// the GPU Scene's static generation and the static external draws' generations.
		std::unique_ptr<S::Texture> shadowStaticTexture;
		bool shadowStaticWritten = false;
		std::uint64_t shadowStaticScene = 0;
		std::uint64_t shadowStaticExternal = 0;
		bool shadowStaticDirty = true;
		bool shadowDynamicExternal = true; // Last frame had shadow draws outside the static layer.

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
		// Screen-space reflections of reflections: last frame's finished screen-space colour
		// (before TAA), read at reflective hits. TAA's own history accumulates many frames, so
		// reading it at a hit showed a moving object's trail - and every bounce between chrome
		// balls added another, older copy.
		std::array<std::unique_ptr<S::Texture>, 2> ssrColorHistory;
		std::uint32_t ssrColorLatest = 0;
		bool ssrColorValid = false;
		bool reflectionHistoryValid = false;
		std::uint64_t shadowFrames = 0;
		std::uint64_t lastShadowFrame = 0;

		// The cascade cache: cascade 0 is drawn every frame, cascade 1 every second frame and
		// the others every fourth (staggered). A cascade is redrawn sooner when its tile moved,
		// the light turned, or the camera moved or turned enough to shift the cascade's slice by
		// a few percent of its radius. A cascade kept this frame keeps the view (and split
		// distance) its depth was drawn with, so lookups stay consistent; moving casters in far
		// cascades lag by at most three frames. Spot and point views are drawn every frame.
		// With the static layer a due cascade keeps its view and redraws only its dynamic
		// casters; the static layer is redrawn when the view moves or static content changed.
		// Returns the imported persistent atlas and fills shadowFrame's Atlas and Render (and
		// StaticAtlas and StaticRender).
		R::GraphTexture PlanShadowCache(R::RenderGraph& graph, R::ShadowPlan& plan, R::ShadowFrame& shadowFrame, const RenderCamera& camera,
			std::span<const R::ShadowCasterDesc> casters, const RenderSettings& settings);

		// (Re)creates the cached atlas and the static layer's for an atlas of this size.
		void EnsureShadowAtlases(std::uint32_t size, bool staticLayer);

		// After the features prepared their shadow draws: a change in the static external draws
		// redraws the static layer next frame.
		void NoteStaticShadowDraws(std::span<const R::ExternalGeometryDraws> draws);

		// Environment (persistent, rebuilt when the sky changes).
		std::unique_ptr<S::Texture> prefiltered;
		std::unique_ptr<S::Buffer> irradiance;
		std::unique_ptr<S::Texture> brdfLut;
		R::EnvironmentMapDesc environmentMap;
		std::optional<R::Environment::ProceduralSky> builtSky;
		bool lutBuilt = false;
		bool environmentValid = false;
		bool environmentHadFeatures = false;  // The last build included feature overlays.
		double environmentBuiltTime = -1.0e9; // featureTime of the last build.
		std::unordered_set<const RenderFeature*> environmentFeatureFailures;
		static constexpr std::uint32_t LutSize = 128;
		static constexpr std::uint32_t LutSamples = 256;

		// History.
		std::optional<std::array<float, 16>> previousViewProjection;
		bool historyBroken = false; // A frame failed: the next starts like a camera cut.
		std::optional<RenderCamera> lastValidCamera;
		bool cameraRejected = false; // The latest camera was unusable (logged once per run of them).
		std::uint64_t frameIndex = 0;
		std::uint32_t residentMeshes = 0; // Stats only: counted every 32 frames (a scan of every mesh).
		std::uint32_t residentTextures = 0;
		double featureTime = 0.0;
		std::unordered_map<std::string, std::unique_ptr<RuntimeComputeProgram>> featurePrograms; // Loaded on first use.
		// Feature geometry programs: space 0 is the pass's frame schema (the union of the
		// engine programs' own), space 1 the shared bindless space.
		std::array<S::DescriptorSchemaDesc, 2> geometryForwardSpaces;
		std::array<S::DescriptorSchemaDesc, 2> geometryShadowSpaces;
		std::map<std::pair<std::string, bool>, std::unique_ptr<RuntimeGraphicsProgram>> geometryPrograms; // (name, shadow).
		// (name, pass, prepassed, cull, samples, alpha to coverage, depth bias, slope depth bias).
		std::map<std::tuple<std::string, std::uint32_t, bool, std::uint32_t, std::uint32_t, bool, float, float>,
			std::unique_ptr<S::GraphicsPipeline>>
			geometryPipelines;
		// Multisampled scene geometry (RenderSettings::MsaaSamples), per sample count: the sky,
		// depth prepass and opaque pipelines and a Forward+ renderer drawing with them. Made on
		// first use and kept (three counts at most) until the renderer goes, so a frame in
		// flight never loses its pipelines when the setting changes.
		struct MsaaPasses
		{
			std::unique_ptr<S::GraphicsPipeline> Sky, Depth, Opaque, Prepassed;
			std::unique_ptr<R::ForwardPlusRenderer> Forward;
		};

		std::map<std::uint32_t, MsaaPasses> msaaPasses;
		std::map<std::uint32_t, std::unique_ptr<S::GraphicsPipeline>> msaaSkyCube; // SkyCubemap per sample count.
		// FrameRenderer::SetSkyCubemap: the faces (kept for a re-upload after an abandoned
		// frame), the cube, and its program and pipelines (made with the first cube map).
		std::vector<std::byte> skyCubeFaces;
		std::uint32_t skyCubeSize = 0;
		std::unique_ptr<S::Texture> skyCube;
		bool skyCubeUploaded = false;
		bool skyCubeFailed = false; // The program or a pipeline could not be made (logged once).
		std::uint64_t skyCubeImportFrame = ~0ull;
		const R::RenderGraph* skyCubeImportGraph = nullptr;
		std::optional<R::GraphTexture> skyCubeImport;
		RuntimeGraphicsProgram skyCubeProgram;
		std::unique_ptr<S::GraphicsPipeline> skyCubePipeline;

		// The cube in this graph, uploaded first when new (one import per graph).
		R::GraphTexture ImportSkyCube(R::RenderGraph& graph);

		// The SkyCubemap pipeline for a sample count; null when unavailable.
		S::GraphicsPipeline* SkyCubePipeline(S::SampleCount samples);
		std::set<std::uint32_t> msaaFailed;					 // Counts whose pipelines could not be made (logged once).
		RuntimeGraphicsProgram forwardDepthCoverage, msaaResolve; // Loaded with the first multisampled frame.
		std::unique_ptr<S::GraphicsPipeline> msaaResolvePipeline;

		// The sample count a frame's scene geometry uses: the setting, lowered to the device's
		// limit, or one sample when the multisampled passes are unavailable.
		S::SampleCount MsaaSampleCount(const RenderSettings& settings);

		// Resolves the multisampled opaque targets into the single-sample ones (ForwardMsaaResolve.slang).
		void RecordMsaaResolve(R::RenderGraph& graph, const R::ForwardPlusTargets& source, const R::ForwardPlusTargets& target,
			std::uint32_t width, std::uint32_t height);
		RuntimeComputeProgram hzbProgram; // Loaded with the first feature that uses occlusion.
		std::unique_ptr<R::HzbBuilder> hzbBuilder;

		struct PendingProgram
		{
			RuntimeShaderDesc Desc;
			std::function<void(std::string_view)> Done;
		};

		std::vector<PendingProgram> pendingPrograms; // ReplaceProgram, applied by ApplyPrograms.

		// Replaced programs, destroyed once the last frame that could use them completed.
		struct RetiredPrograms
		{
			S::TimelinePoint After;
			std::vector<std::unique_ptr<RuntimeComputeProgram>> Compute;
			std::vector<std::unique_ptr<RuntimeGraphicsProgram>> Graphics;
			std::vector<std::unique_ptr<S::GraphicsPipeline>> Pipelines;
		};

		std::vector<RetiredPrograms> retiredPrograms;

		void CollectPrograms();
		std::function<void(std::string_view, const RuntimeComputeProgram&, std::span<std::byte>)> overrideConstants;

		RenderFeatureGeometryPipeline GetGeometryPipeline(
			std::string_view name, RenderGeometryPass pass, bool prepassed, const RenderGeometryState& state, S::SampleCount samples);

		std::unique_ptr<S::GraphicsPipeline> CreateGeometryPipeline(std::string_view name, const RuntimeGraphicsProgram& program,
			RenderGeometryPass pass, bool prepassed, S::CullMode cull, S::SampleCount samples, bool alphaToCoverage, float depthBias,
			float slopeBias);

		// Before recording a frame; after is the last submission (the old programs' last use).
		void ApplyPrograms(S::TimelinePoint after);

		RenderFeatureContext::Services FeatureServices();

		// The frame state the scene passes (shadows, visibility, Forward+) record from.
		struct SceneInputs
		{
			R::RenderGraph& Graph;
			const RenderSettings& Settings;
			const RenderFrameInput& Input;
			RenderStats& Stats;
			std::span<const std::shared_ptr<RenderFeature>> Features;
			const RenderFeatureView& FeatureView;
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			const std::array<float, 16>& ViewProjection;
			const std::array<float, 2>& Jitter;
			const Engine::GpuGlint& Glint;
			const R::RenderViewDesc& ViewDesc;
			const std::vector<R::ForwardPlusPageSlot>& ForwardSlots;
			const std::vector<R::ShadowPageSlot>& ShadowSlots;
			const std::vector<std::uint32_t>& IndexPages;
			const R::GeometryGraphResources& Geometry;
			const R::GpuMaterialGraphResources& Materials;
			const R::GpuSceneGraphResources& Scene;
			const R::GpuLightGraphResources& Lights;
			const std::optional<R::EnvironmentGraphResources>& Environment;
			const std::optional<R::GraphTexture>& Lut;
			const R::ForwardPlusTargets& Targets;
			bool Draw3D = false;
			bool Temporal = false;
			// Multisampled frames: the targets the sky and opaque passes draw into, resolved into Targets.
			const R::ForwardPlusTargets* MsaaTargets = nullptr;
		};

		// Draws the features add to the geometry passes; prepared once per frame, after the shadow plan.
		struct FeatureGeometry
		{
			std::vector<R::ExternalGeometryDraws> Draws;
			std::vector<RenderFeatureShadowView> ShadowViews;
			bool Prepared = false;
		};

		// What the scene passes produced; the Forward+ frame points into it.
		struct ScenePasses
		{
			std::optional<R::ShadowGraphResources> Shadows;
			std::optional<R::VisibilityGraphResources> Visible;
			R::ClusterGridDesc Grid;
			std::optional<R::ClusterGraphResources> Clusters;
			R::ForwardPlusFrame Forward;
		};

		struct EnvironmentInputs
		{
			R::RenderGraph& Graph;
			const RenderSettings& Settings;
			const RenderFeatureView& FeatureView;
			std::span<const std::shared_ptr<RenderFeature>> Features;
			bool Draw3D = false;
		};

		struct EnvironmentOutputs
		{
			std::optional<R::EnvironmentGraphResources>& Environment;
			std::optional<R::GraphTexture>& Lut;
			bool& Rebuilt;
			bool& WithFeatures;
		};

		// The BRDF LUT and the environment maps (rebuilt when the sky or its feature overlays change).
		void RecordEnvironment(const EnvironmentInputs& in, const EnvironmentOutputs& out);

		R::GraphTexture RunFeatures(const SceneInputs& in, RenderFeatureStage stage, R::GraphTexture color);

		R::GraphTexture RecordPost(
			const SceneInputs& in, R::GraphTexture sceneOutput, const std::optional<R::ParticleGraphResources>& particles);

		// Draws the UI over target by layer, with the screen blur between layers when
		// RenderSettings::Blur.AfterUiLayer asks for it; returns the final frame.
		R::GraphTexture RecordUi(const SceneInputs& in, R::GraphTexture target, const S::TimelinePoint& lastCompletion, bool& atlasPending,
			bool& viewportsPending);

		void PrepareFeatureGeometry(const SceneInputs& in, FeatureGeometry& geometry);

		void RecordShadows(const SceneInputs& in, FeatureGeometry& geometry, ScenePasses& out);

		void RecordVisibility(const SceneInputs& in, ScenePasses& out);

		// SceneInputs must outlive the frame's graph recording: the Forward+ hooks read it.
		std::optional<R::ForwardPlusGraphResources> RecordForward(const SceneInputs& in, const FeatureGeometry& geometry, ScenePasses& out);

		// Clears every Forward+ target; rayRight/rayUp are the view's right and up scaled by the
		// half-angle tangents.
		void RecordSky(R::RenderGraph& graph, const RenderSettings& settings, const R::ForwardPlusTargets& skyTargets,
			const std::array<float, 3>& rayRight, const std::array<float, 3>& rayUp, const std::array<float, 3>& rayForward,
			std::uint32_t skyWidth, std::uint32_t skyHeight, const char* passName = "Sky background");

		// The main view's frame state the reflection captures render from.
		struct CaptureInputs
		{
			R::RenderGraph& Graph;
			const RenderSettings& Settings;
			const RenderFrameInput& Input;
			RenderStats& Stats;
			const std::vector<std::uint32_t>& IndexPages;
			const R::GeometryGraphResources& Geometry;
			const R::GpuSceneGraphResources& Scene;
			const R::GpuLightGraphResources& Lights;
			const std::optional<R::EnvironmentGraphResources>& Environment;
			const R::ForwardPlusTargets& Targets;
			const R::ClusterGridDesc& Grid;
			const R::ForwardPlusFrame& ForwardFrame;
		};

		void RecordReflectionProbes(const CaptureInputs& in, std::optional<R::ScreenSpaceFrame::ProbeInputs>& probeInputs);
		std::vector<R::GpuReflectionProbeRecord> ActiveProbeRecords(
			const R::ReflectionProbes::Plan& plan, std::span<const R::ReflectionProbeDesc> probes) const;
		void RecordProbeCapture(const CaptureInputs& in, const R::ReflectionProbes::FaceCapture& capture,
			const R::ReflectionProbeRenderer::Atlases& atlases, const std::optional<R::ReflectionCaptureProbes>& captureProbes);
		void RecordProbeFilters(R::RenderGraph& graph, const R::ReflectionProbes::Plan& plan,
			std::span<const R::ReflectionProbeDesc> probes, const R::ReflectionProbeRenderer::Atlases& atlases, std::size_t filtersPerFrame,
			std::uint32_t prefilterSamples);

		void RecordPlanarReflections(const CaptureInputs& in, std::uint32_t width, std::uint32_t height,
			const std::array<float, 16>& viewProjection, const std::optional<R::ScreenSpaceFrame::ProbeInputs>& probeInputs,
			std::optional<R::ScreenSpaceFrame::PlanarInputs>& planarInputs);

		struct ScreenSpaceInputs
		{
			R::RenderGraph& Graph;
			const RenderSettings& Settings;
			const RenderCamera& Camera;
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			const std::array<float, 2>& Jitter;
			bool Temporal = false;
			bool Draw3D = false;
			const R::ForwardPlusTargets& Targets;
			const std::optional<R::ScreenSpaceFrame::ProbeInputs>& Probes;
			const std::optional<R::ScreenSpaceFrame::PlanarInputs>& Planar;
		};

		R::ScreenSpaceGraphResources RecordScreenSpace(const ScreenSpaceInputs& in);

		// Uploads and frame work a frame staged: aborted if recording throws, committed once submitted.
		struct FramePending
		{
			bool Residency = false;
			bool Materials = false;
			bool Scene = false;
			bool Lights = false;
			bool Particles = false;
			bool Skinning = false;
			bool Atlas = false;
			bool Viewports = false;
		};

		void AbortFrame(const FramePending& pending);

		void CommitFrame(const FramePending& pending, const S::TimelinePoint& completion);

		// Times a frame's consecutive CPU phases, each from the end of the one before.
		struct CpuLaps
		{
			std::vector<RenderStats::PassTiming>& Phases;
			std::chrono::steady_clock::time_point Start;

			void operator()(const char* name)
			{
				const auto now = std::chrono::steady_clock::now();
				Phases.push_back({ name, std::chrono::duration<double, std::milli>(now - Start).count() });
				Start = now;
			}
		};

		struct FrameBuildInputs
		{
			R::RenderGraph& Graph;
			RenderDevice& Device;
			const RenderDevice::Frame& Frame;
			const RenderSettings& Settings;
			const RenderFrameInput& Input;
			RenderStats& Stats;
			std::span<const std::shared_ptr<RenderFeature>> Features;
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			const std::array<float, 16>& ViewProjection;
			const std::array<float, 2>& Jitter;
			const Engine::GpuGlint& Glint;
			const S::TimelinePoint& LastCompletion;
			CpuLaps& Lap;
			bool LateAcquire = false;
			bool Cut = false;
			bool Temporal = false;
		};

		struct FrameBuildOutputs
		{
			FramePending Pending;
			bool EnvironmentRebuilt = false;
			bool EnvironmentWithFeatures = false;
			std::optional<R::GraphReadback> Capture;
			std::optional<S::TimelinePoint> Submitted; // The executor submitted the frame.
		};

		// Builds, compiles and executes the frame's graph; the scene inputs it records from stay
		// alive until the executor has recorded every pass.
		S::TimelinePoint BuildAndExecute(const FrameBuildInputs& in, FrameBuildOutputs& out);

		// Shadows, visibility, Forward+ and the reflection captures of a frame with 3D draws.
		void RecordScene(const SceneInputs& in, FeatureGeometry& geometry, CpuLaps& lap,
			std::optional<R::ScreenSpaceFrame::ProbeInputs>& probeInputs, std::optional<R::ScreenSpaceFrame::PlanarInputs>& planarInputs);

		std::optional<R::ParticleGraphResources> SimulateParticles(const FrameBuildInputs& in, FramePending& pending);

		// The camera's bindless glint texture index (0 without one).
		std::uint32_t GlintIndex();

		void ResetHistories(bool cameraCut, bool temporalOn);

		// The late acquire after the frame is submitted, and the copy of the held frame into the
		// image; the device's completion (the copy's, or completion without an image).
		S::TimelinePoint AcquireAndCopy(RenderDevice& device, RenderDevice::Frame& frame, S::TimelinePoint completion, std::uint32_t width,
			std::uint32_t height, CpuLaps& lap);

		// The state a submitted frame leaves for the next one.
		void FinishFrame(const RenderSettings& settings, const FrameBuildOutputs& built, const std::array<float, 16>& viewProjection);

		// The frame's persistent GPU state, imported into the graph (skinning recorded as well).
		struct FrameImports
		{
			R::GpuResidencyGraphResources Residency;
			R::GpuMaterialGraphResources Materials;
			R::GpuSceneGraphResources Scene;
			R::GpuLightGraphResources Lights;
		};

		FrameImports ImportFrame(R::RenderGraph& graph, FramePending& pending);

		// The view render features see this frame (advances the feature clock by the frame's delta).
		RenderFeatureView BuildFeatureView(const RenderFrameInput& input, const RenderSettings& settings,
			const std::array<float, 16>& viewProjection, const std::array<float, 2>& jitter, bool cut, bool temporal, std::uint32_t width,
			std::uint32_t height);

		void FillFrameStats(RenderStats& stats, std::uint32_t width, std::uint32_t height);

		void RecordPresentation(R::RenderGraph& graph, const RenderDevice::Frame& frame, bool lateAcquire, R::GraphTexture postOutput,
			std::uint32_t width, std::uint32_t height);

		S::TimelinePoint PresentHeldFrame(const RenderDevice::Frame& frame, std::uint32_t width, std::uint32_t height);

		void ReturnImage(const RenderDevice::Frame& frame, std::uint32_t width, std::uint32_t height);

		void Route(std::uint32_t set, const R::StandardPbr::Parameters& parameters);

		void EnsureVisibility(std::uint32_t slots);

		RuntimeGraphicsProgram LoadDrawProgram(std::string_view name, const S::DescriptorSchemaDesc* bindlessSpace);

		S::GraphicsPipeline& GetPresentPipeline(S::Format format);

		// The frame held for a late acquire (async present), per frame slot.
		struct Held
		{
			std::unique_ptr<S::Texture> Texture;
			S::ResourceState State = S::ResourceState::Undefined;
		};

		std::vector<Held> heldFrames;
		// Frames of an unchanged shape reuse the compiled schedule, transients and descriptor tables.
		Swim::Render::RenderGraphCompileCache graphCache;
		Swim::Render::RenderGraphCompileCache presentGraphCache;

		Held& HeldFrame(std::uint32_t slot, std::uint32_t width, std::uint32_t height);

		void EnsureEnvironmentTargets(std::uint32_t resolution);
	};

} // namespace Engine
