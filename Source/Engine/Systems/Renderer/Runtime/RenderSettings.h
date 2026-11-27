#pragma once

#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"
#include "Engine/Systems/Renderer/Features/Glint.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessSettings.h"
#include "Engine/Systems/Renderer/Reflections/PlanarReflectionTypes.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeTypes.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceSettings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Renderer/Temporal/TemporalSettings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Engine
{

	// How the frame is anti-aliased. Fxaa and Smaa1x run on the display-referred frame after
	// tone mapping (no history, no jitter, nothing smears); Taa resolves jittered history.
	enum class AntiAliasingMode : std::uint8_t
	{
		Off,
		Fxaa,
		Smaa1x,
		Taa,
	};

	// The first-person overlay layer (RenderObjectFlags::Overlay on MeshRenderer::Flags):
	// held items and arms drawn in front of the whole scene with their own projection.
	struct OverlaySettings
	{
		float FieldOfView = 0.0f; // Vertical, degrees; 0 = the camera's.
		float Near = 0.01f;		  // Near plane (metres) of the overlay's depth.
	};

	// Every runtime rendering switch (Phase 23). The FrameRenderer reads it each frame;
	// the sandbox control panel edits it live. Invalid values are clamped by Sanitize.
	struct RenderSettings
	{
		// Lighting.
		std::array<float, 3> Ambient{ 0.015f, 0.017f, 0.02f };
		// Full bright (accessibility, night vision): the lowest diffuse light any opaque
		// surface lit by the forward passes receives, as radiance on a white surface (0: off;
		// about 0.3-1 for a scene that is never dark). Lit and shadowed areas keep their
		// difference above the floor, and the floor itself is shaded by the normal.
		float MinimumLight = 0.0f;
		// The sheen of glinting materials (MaterialDesc::Glint) and UI images (UiImage::Glint):
		// Procedural (enhanced) or Texture (the classic scrolling layers of the texture given to
		// FrameRenderer::SetGlintTexture), or Off.
		GlintSettings Glint;
		bool Environment = true;   // Image-based lighting from the procedural sky.
		bool SkyBackground = true; // Draw the sky behind the scene (else ClearColor).
		// Linear radiance scale of a FrameRenderer::SetSkyCubemap sky (its sRGB colours at 1).
		float SkyCubemapIntensity = 1.0f;
		std::array<float, 3> ClearColor{ 0.02f, 0.02f, 0.025f };
		Swim::Render::Environment::ProceduralSky Sky{};
		// The colour the sky background draws below the horizon, when set; lighting and the
		// reflection fallback keep Sky.GroundColor. (A scene on a large floor can light and
		// reflect with a floor-coloured ground while the visible void stays sky blue.)
		std::optional<std::array<float, 3>> SkyBackgroundGround;
		float EnvironmentIntensity = 1.0f;
		float EnvironmentRotation = 0.0f; // Radians about +Y.
		OverlaySettings Overlay{};
		std::uint32_t EnvironmentResolution = 128;
		// How often (seconds) the environment is re-recorded while a render feature draws
		// into it (RenderFeature::ContributesToEnvironment, e.g. drifting clouds); 0 = every frame.
		float EnvironmentRefreshSeconds = 0.5f;
		// Whether those feature contributions also change the ambient (SH irradiance) light.
		// Off, they show only in reflections and the ambient stays the clear sky's (bright
		// clouds otherwise lift every shadow).
		bool EnvironmentFeatureAmbient = false;

		// Local reflection probes (RenderFrameInput::ReflectionProbes): the layer between
		// screen-space reflections and the global environment.
		Swim::Render::ReflectionProbeSettings ReflectionProbes{};

		// Planar reflections (RenderFrameInput::PlanarReflectors): sharp, current mirrors on top
		// of the hierarchy, captured at a fraction of their screen size, shared between
		// coplanar faces and re-rendered only when they need it.
		Swim::Render::PlanarReflectionSettings PlanarReflections{};

		// Shadows.
		bool Shadows = true;
		// Keep directional cascades between frames: cascade 1 is redrawn every second frame,
		// the others every fourth (sooner when the camera or light moves enough); see
		// FrameRenderer's PlanShadowCache.
		bool ShadowCascadeCache = true;
		// With the cascade cache: static casters (RenderObjectFlags::Static objects and
		// ExternalGeometryDraws::StaticShadow draws) are kept in a second atlas and redrawn only
		// when the cascade moves or they change; each cascade refresh copies them and draws the
		// dynamic casters on top.
		bool ShadowStaticLayer = true;
		// With the static layer: every cascade redraws its dynamic casters every frame (moving
		// shadows never lag); the static layer still follows the cascade's own refreshes.
		bool ShadowDynamicEveryFrame = false;
		Swim::Render::ShadowSettings Shadow{};

		// Clustered lights.
		std::uint32_t ClusterTileSize = 64;
		std::uint32_t ClusterSlices = 32;
		float ClusterFar = 200.0f;
		std::uint32_t MaxLightsPerCluster = 128; // Heatmap full scale only: cluster light sets are bitmasks, never truncated.

		// Profiling switches: every one of these defaults to the normal frame.
		bool LocalLights = true;		// Point and spot lights (off: only directional lights are uploaded).
		// Point and spot lights shaded in a compute pass after the opaque pass (same result;
		// no helper lanes, a small high-occupancy program) instead of in its fragment stage.
		bool DeferredLocalLights = true;
		bool Transparent = true;		// The Forward+ transparent sort and pass.
		bool EnvironmentUpdates = true; // Re-record the environment for feature overlays (clouds); off freezes it.

		// Effects.
		bool Particles = true;
		bool TemporalAntiAliasing = true; // Taa only (false: no anti-aliasing while Taa is chosen).
		AntiAliasingMode AntiAliasing = AntiAliasingMode::Taa;
		// Multisample anti-aliasing of the scene geometry: 1 (off), 2, 4, 8 or 16 samples, lowered
		// to the largest count the device supports at or below it (FrameRenderer::
		// GetSupportedMsaaSamples lists them; RenderStats::MsaaSamples is the count a frame used). The sky,
		// depth prepass and opaque pass draw multisampled and are resolved before the transparent
		// layers and every screen-space pass; works with any AntiAliasing mode, TAA on or off.
		// Alpha-masked materials use alpha to coverage. Memory: the seven opaque targets and
		// depth, times the sample count, on top of the single-sample ones.
		std::uint32_t MsaaSamples = 1;
		// FXAA/SMAA smooth only geometric edges (depth steps, normal turns) and keep corners
		// square, so texture texel edges stay hard; false: every luma edge (classic).
		bool CrispAntiAliasing = true;
		float Sharpening = 0.25f;	  // Contrast-adaptive sharpening after anti-aliasing, 0..1 (0 = off); restores what TAA/FXAA soften.
		bool CinematicEffects = true; // Off: render features with IsCinematic() are skipped.

		// Blur of the finished frame (after post-processing, anti-aliasing and the
		// AfterPostProcess features, before the UI), for backdrops behind menus. A dual-filter
		// (Kawase) down/up chain in linear light, about 1 to 6 half-size levels.
		struct ScreenBlurSettings
		{
			// 0 = off (no passes, no cost) .. 1 = strongest (a radius of about 48 pixels at
			// 1080p, scaled with the frame height). Small values fade the blur in.
			float Strength = 0.0f;
			// 0 = the whole frame; else only inside Regions[0 .. RegionCount) (at most 4),
			// pixel rectangles { x0, y0, x1, y1 } in the frame's pixels (x1, y1 exclusive),
			// for blurring only behind UI panels. The chain still covers the whole frame.
			std::uint32_t RegionCount = 0;
			std::array<std::array<float, 4>, 4> Regions{};
			// Where the blur runs: unset = before any UI; L = after the UI items whose
			// UiDrawItem::Layer is <= L, so those layers are blurred with the scene and the
			// higher layers are drawn sharp over it. Applies even when no UI is drawn.
			std::optional<std::int32_t> AfterUiLayer;
		};

		ScreenBlurSettings Blur{};
		Swim::Render::TemporalSettings Temporal{};
		Swim::Render::ScreenSpaceSettings ScreenSpace{};
		Swim::Render::PostProcessSettings Post{};

		// Bytes a StreamingBuffer uploads per frame (render features pass it to Import;
		// the console variable render_upload_budget_mb).
		std::uint64_t StreamingUploadBudget = 16ull << 20;

		// Overlays.
		bool Ui = true;
		Swim::Render::ForwardPlusDebugMode Debug = Swim::Render::ForwardPlusDebugMode::None;

		// Clamps every value into its valid range (keeps the renderer from throwing on a
		// slider pushed to an extreme).
		void Sanitize();
	};

	// One frame's diagnostics (the overlay and tests read them).
	struct RenderStats
	{
		std::uint64_t Frame = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		double CpuMilliseconds = 0.0; // Recording + submission.
		double GpuMilliseconds = 0.0; // Sum of pass timings (previous frame), 0 when unsupported.
		bool GpuTimingsAvailable = false;
		std::uint32_t Passes = 0;
		std::uint32_t RenderObjects = 0; // Live GPU Scene objects.
		std::uint32_t Materials = 0;
		std::uint32_t DirectionalLights = 0;
		std::uint32_t LocalLights = 0;
		std::uint32_t ShadowViews = 0;
		std::uint32_t ShadowViewsRendered = 0; // Shadow views re-rendered this frame (the rest reuse the cache).
		std::uint32_t ShadowCasters = 0;
		std::uint32_t ParticleEmitters = 0;
		std::uint32_t SkinnedInstances = 0;
		std::uint32_t UiQuads = 0;
		std::uint32_t PageSlots = 0;
		std::uint32_t ResidentMeshes = 0;
		std::uint32_t ResidentTextures = 0;
		std::uint32_t PendingAssets = 0;
		bool Rendered3D = false; // False until the first mesh is GPU-resident.
		std::uint32_t ReflectionProbes = 0;		// Probes shading used this frame.
		std::uint32_t ReflectionProbeFaces = 0; // Probe cube faces captured this frame.
		std::uint32_t PlanarReflections = 0;	// Planar reflections shading used this frame.
		std::uint32_t PlanarCaptures = 0;		// Planar captures rendered this frame.
		std::uint32_t PlanarCandidates = 0;		// Reflector faces considered (before culling and merging).
		bool Presented = false;
		std::uint64_t SkippedFrames = 0; // Minimized, out-of-date swapchain.
		std::uint32_t MsaaSamples = 1;	 // Samples the scene geometry used this frame (RenderSettings::MsaaSamples, as supported).
		bool SkyCubemap = false;		 // The sky background drew the FrameRenderer::SetSkyCubemap cube this frame.
		std::uint32_t BlurLevels = 0;	 // Screen blur (RenderSettings::Blur) half-size levels this frame, 0 = off.

		struct PassTiming
		{
			std::string Name;
			double Milliseconds = 0.0;
		};

		// The costliest passes of the previous measured frame, largest first.
		std::array<PassTiming, 8> TopPasses{};
		std::uint32_t TopPassCount = 0;
		// Every timed GPU pass of the previous frame, in execution order.
		std::vector<PassTiming> GpuPasses;
		// This frame's CPU time in Render by phase (waiting for the GPU, each graph section,
		// compile, record and submit, present), in order.
		std::vector<PassTiming> CpuPhases;
		// Inside "Record and submit": the executor's steps and each pass callback's CPU time.
		std::vector<PassTiming> RecordPhases;
		std::vector<PassTiming> RecordPasses;
		// CPU time of each render feature's PrepareGeometry ("<name>: geometry") and Record
		// ("<name>: record") this frame; these overlap the CpuPhases that contain them.
		std::vector<PassTiming> FeaturePhases;
	};

} // namespace Engine
