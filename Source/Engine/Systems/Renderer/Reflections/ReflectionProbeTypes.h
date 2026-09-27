#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// One reflection probe as the scene describes it (Engine::ReflectionProbe on an entity,
	// gathered by SceneRenderBridge). A probe is a point the renderer captures the scene
	// from into a small cube map (time-sliced, a few faces per frame), prefilters for every
	// roughness and stores with the distance to what each texel sees, so shading can
	// correct the parallax between the probe and the reflecting surface.
	struct ReflectionProbeDesc
	{
		std::uint64_t Key = 0;				 // Stable identity (the entity); slots follow it.
		std::array<float, 3> Position{};	 // Capture point (world).
		float InfluenceRadius = 4.0f;		 // Surfaces within this distance use the probe...
		float BlendDistance = 1.0f;			 // ...fading out over the last BlendDistance metres.
		float CaptureNear = 0.05f;			 // Near plane of the capture views.
		std::uint32_t OwnerObjectId = 0;	 // GpuInstanceRecord::ObjectId + 1 of an object probe (0: area probe).
		float Priority = 1.0f;				 // Scales the update urgency.
		bool Dynamic = true;				 // Re-captured continuously (moving objects around it); static: once, and when it moves.
	};

	// Something that moved this frame (a bounding sphere): the probe faces that see it are
	// re-captured first, so moving objects show up in reflections without waiting for the
	// round-robin refresh (SceneRenderBridge gathers them from moved mesh entities).
	struct ReflectionProbeMover
	{
		std::array<float, 3> Center{};
		float Radius = 0.5f;
	};

	// Renderer-wide probe controls (RenderSettings::ReflectionProbes).
	struct ReflectionProbeSettings
	{
		bool Enabled = true;
		std::uint32_t Resolution = 128;	  // Face size (power of two, 16 .. 512).
		std::uint32_t MaxProbes = 8;	  // Slots of the probe atlas (1 .. MaxReflectionProbes).
		std::uint32_t FacesPerFrame = 2;  // Cube faces captured per frame (0 .. 12): the cost bound.
		float MoveThreshold = 0.05f;	  // A probe that moved this far is re-captured with priority.
		float MoverRange = 15.0f;		  // Movers farther than this from a probe do not raise its faces' urgency.
		std::uint32_t PrefilterSamples = 32;
		std::uint32_t FiltersPerFrame = 2; // Probes prefiltered per frame (each: 6 faces x every mip); the rest wait their turn.
		// Dynamic faces that see no mover (and whose probe did not move) are refreshed once
		// they are this many frames old (0: every frame the budget allows, round-robin). Faces
		// that see movers still go first every frame.
		std::uint32_t IdleRefreshFrames = 0;
	};

	inline constexpr std::uint32_t MaxReflectionProbes = 16;

	// GPU row of one active probe (Shaders/Slang/ScreenSpace/ScreenSpaceComposite.slang).
	struct GpuReflectionProbeRecord
	{
		float PositionRadius[4] = {}; // xyz: capture position; w: influence radius.
		// x: blend distance; y: atlas slot (cube index); z: age in seconds (oldest face);
		// w: owner ObjectId + 1 (exact as a float below 2^24; 0: area probe).
		float Params[4] = {};
	};

	static_assert(sizeof(GpuReflectionProbeRecord) == 32);
} // namespace Swim::Render
