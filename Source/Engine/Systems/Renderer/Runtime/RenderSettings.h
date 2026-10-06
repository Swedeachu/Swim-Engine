#pragma once

#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"
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

	// Every runtime rendering switch (Phase 23). The FrameRenderer reads it each frame;
	// the sandbox control panel edits it live. Invalid values are clamped by Sanitize.
	struct RenderSettings
	{
		// Lighting.
		std::array<float, 3> Ambient{ 0.015f, 0.017f, 0.02f };
		bool Environment = true;   // Image-based lighting from the procedural sky.
		bool SkyBackground = true; // Draw the sky behind the scene (else ClearColor).
		std::array<float, 3> ClearColor{ 0.02f, 0.02f, 0.025f };
		Swim::Render::Environment::ProceduralSky Sky{};
		// The colour the sky background draws below the horizon, when set; lighting and the
		// reflection fallback keep Sky.GroundColor. (A scene on a large floor can light and
		// reflect with a floor-coloured ground while the visible void stays sky blue.)
		std::optional<std::array<float, 3>> SkyBackgroundGround;
		float EnvironmentIntensity = 1.0f;
		float EnvironmentRotation = 0.0f; // Radians about +Y.
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
		bool TemporalAntiAliasing = true;
		Swim::Render::TemporalSettings Temporal{};
		Swim::Render::ScreenSpaceSettings ScreenSpace{};
		Swim::Render::PostProcessSettings Post{};

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
	};

} // namespace Engine
