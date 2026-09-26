#pragma once

#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessSettings.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceSettings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Renderer/Temporal/TemporalSettings.h"

#include <array>
#include <cstdint>
#include <string>

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
		float EnvironmentIntensity = 1.0f;
		float EnvironmentRotation = 0.0f; // Radians about +Y.
		std::uint32_t EnvironmentResolution = 128;

		// Shadows.
		bool Shadows = true;
		Swim::Render::ShadowSettings Shadow{};

		// Clustered lights.
		std::uint32_t ClusterTileSize = 64;
		std::uint32_t ClusterSlices = 24;
		float ClusterFar = 200.0f;
		std::uint32_t MaxLightsPerCluster = 128;

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
	};
} // namespace Engine
