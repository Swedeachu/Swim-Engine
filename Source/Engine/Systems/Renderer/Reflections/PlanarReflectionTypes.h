#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{

	// What a planar reflector reflects with: its flat faces, or (spheres) the central disc
	// that faces the camera.
	enum class PlanarReflectorShape : std::uint8_t
	{
		Plane,	// The local XZ plane (normal +Y), PlaneHalfExtents across: mirrors, glossy floors.
		Box,	// The six faces of the unit cube (+-0.5): mirror cubes, glass blocks, walls.
		Sphere, // A unit sphere's camera-facing cap (radius Radius): chrome balls.
	};

	// One planar reflector as the scene describes it (Engine::PlanarReflector on an entity,
	// gathered by SceneRenderBridge).
	struct PlanarReflectorDesc
	{
		std::uint64_t Key = 0;			  // Stable identity (the entity); capture slots follow it.
		std::uint32_t OwnerObjectId = 0;  // GpuInstanceRecord::ObjectId + 1: culled from its own captures.
		PlanarReflectorShape Shape = PlanarReflectorShape::Box;
		std::array<float, 12> World{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }; // Local -> world, row-major 3x4.
		std::array<float, 2> PlaneHalfExtents{ 0.5f, 0.5f };			 // Plane: local X/Z half extents.
		float Radius = 0.5f;											 // Sphere: local radius (scaled by World).
		float Quality = 1.0f;											 // Resolution multiplier.
		float Priority = 1.0f;											 // Capture urgency multiplier.
	};

	inline constexpr std::uint32_t MaxPlanarReflectionSlots = 12;

	// Renderer-wide planar reflection controls (RenderSettings::PlanarReflections).
	struct PlanarReflectionSettings
	{
		bool Enabled = true;
		std::uint32_t MaxPlanes = 8;		  // Capture slots (atlas layers), 1 .. MaxPlanarReflections.
		std::uint32_t AtlasResolution = 1024; // Layer size: the largest capture (power of two, 64 .. 2048).
		std::uint32_t MinResolution = 64;	  // The smallest capture side.
		// Re-renders per frame (the cost bound): captures whose content changed first (a new or
		// moved reflector, a portal the capture no longer covers, something moving in or leaving
		// its view), then the rest that need it (camera motion, LOD, age), most urgent first.
		// The default re-renders every visible capture that needs it every frame: planar
		// reflections run at the full frame rate, never reprojected from an older frame.
		std::uint32_t CapturesPerFrame = MaxPlanarReflectionSlots;
		// Capture texels per screen pixel the reflector covers (LOD: far reflectors get fewer).
		float ResolutionScale = 1.0f;
		// Captures render at this many times their stored size (each side, 1 .. 4) and are
		// averaged down when resolved. 1 (the default) relies on the capture jitter instead:
		// captures re-rendered every frame follow the camera's TAA jitter, so TAA anti-aliases
		// the reflection at no extra cost. Lowered for a capture whose rendered side would
		// exceed MaxRenderResolution.
		std::uint32_t Supersample = 1;
		std::uint32_t MaxRenderResolution = 1536;
		// Reflectors smaller than this fraction of the screen height use SSR and probes only.
		float MinScreenFraction = 0.03f;
		// The far LOD of the hierarchy: below this screen fraction a reflector hands over to
		// screen-space reflections gradually (where SSR has a confident hit), completely at
		// MinScreenFraction. Above it planar reflections are never replaced by SSR.
		float SsrFallbackScreenFraction = 0.1f;
		// Faces within these tolerances share one capture (the reflected camera is offset to
		// the nearest; the lookup's depth march corrects the rest).
		float PlaneAngleTolerance = 0.035f;	 // Radians.
		float PlaneDistanceTolerance = 0.05f; // Metres.
		// A capture is re-rendered when the camera moved more than this many capture texels
		// since (less is reprojected exactly enough) or when older than MaxAgeSeconds (animated
		// materials, lighting; 0: every frame, the default - full-rate reflections).
		float MotionTolerance = 0.25f;
		float MaxAgeSeconds = 0.0f;
		// Movers (ReflectionProbeMover) farther than this from a reflector do not make its capture
		// dynamic (distant motion is a few texels; a busy far crowd would otherwise re-render it
		// every frame).
		float MoverRange = 25.0f;
		// Spheres reflect through their central cap: surface normals within acos of this of
		// the camera axis; the rim falls back to the object probe.
		float SphereMinCosine = 0.75f;
		// Surfaces fade from the planar reflection to the fallback between these roughnesses.
		float RoughnessFadeStart = 0.15f;
		float RoughnessFadeEnd = 0.35f;
		// Portals are grown by this fraction so a capture survives some camera motion.
		float PortalMargin = 0.1f;
		// Geometry farther than this from the reflected camera is culled from captures.
		float CullDistance = 150.0f;
	};

	inline constexpr std::uint32_t MaxPlanarReflections = MaxPlanarReflectionSlots;

	// GPU row of one active planar reflection (Shaders/Slang/ScreenSpace/ScreenSpaceComposite.slang).
	struct GpuPlanarReflectionRecord
	{
		float ViewProjection[16] = {}; // World -> capture clip (row-major), as captured.
		float Plane[4] = {};		   // xyz: normal (towards viewers), w: n . p of the plane.
		float Camera[4] = {};		   // xyz: capture position; w: owner ObjectId + 1 (0: any surface on the plane).
		float Atlas[4] = {};		   // x: layer; y, z: the capture's share of the layer (u, v); w: SSR preference (far LOD only).
		float Params[4] = {};		   // x: plane tolerance (m); y: min normal cosine; z, w: roughness fade start, end.
	};

	static_assert(sizeof(GpuPlanarReflectionRecord) == 128);

} // namespace Swim::Render
