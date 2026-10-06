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

	// Renderer-wide planar reflection controls (RenderSettings::PlanarReflections).
	struct PlanarReflectionSettings
	{
		bool Enabled = true;
		std::uint32_t MaxPlanes = 6;		 // Capture slots (atlas layers), 1 .. MaxPlanarReflections.
		std::uint32_t AtlasResolution = 512; // Layer size: the largest capture (power of two, 64 .. 2048).
		std::uint32_t MinResolution = 48;	 // The smallest capture side.
		std::uint32_t CapturesPerFrame = 2;	 // Re-renders per frame: the cost bound (0 freezes them).
		// Capture texels per screen pixel the reflector covers (LOD: far reflectors get fewer).
		float ResolutionScale = 0.5f;
		// Reflectors smaller than this fraction of the screen height use SSR and probes only.
		float MinScreenFraction = 0.04f;
		// Planes whose mirror ray at their centre mostly stays on screen (dot with the view
		// direction above this) are left to SSR, which shows them at full resolution.
		float SsrHandoff = 0.85f;
		// Faces within these tolerances share one capture (the reflected camera is offset to
		// the nearest; the lookup's depth march corrects the rest).
		float PlaneAngleTolerance = 0.035f;	 // Radians.
		float PlaneDistanceTolerance = 0.05f; // Metres.
		// A capture is re-rendered when the camera moved this many capture texels since (the
		// lookup reprojects the rest), when something moved inside its view, or when older
		// than MaxAgeSeconds (animated content).
		float MotionTolerance = 3.0f;
		float MaxAgeSeconds = 0.25f;
		// Spheres reflect through their central cap: surface normals within acos of this of
		// the camera axis; the rim falls back to the object probe.
		float SphereMinCosine = 0.9f;
		// Surfaces fade from the planar reflection to the fallback between these roughnesses.
		float RoughnessFadeStart = 0.15f;
		float RoughnessFadeEnd = 0.35f;
		// Portals are grown by this fraction so a capture survives some camera motion.
		float PortalMargin = 0.1f;
		// Geometry farther than this from the reflected camera is culled from captures.
		float CullDistance = 150.0f;
	};

	inline constexpr std::uint32_t MaxPlanarReflections = 8;

	// GPU row of one active planar reflection (Shaders/Slang/ScreenSpace/ScreenSpaceComposite.slang).
	struct GpuPlanarReflectionRecord
	{
		float ViewProjection[16] = {}; // World -> capture clip (row-major), as captured.
		float Plane[4] = {};		   // xyz: normal (towards viewers), w: n . p of the plane.
		float Camera[4] = {};		   // xyz: capture position; w: owner ObjectId + 1 (0: any surface on the plane).
		float Atlas[4] = {};		   // x: layer; y, z: the capture's share of the layer (u, v); w: SSR preference.
		float Params[4] = {};		   // x: plane tolerance (m); y: min normal cosine; z, w: roughness fade start, end.
	};

	static_assert(sizeof(GpuPlanarReflectionRecord) == 128);

} // namespace Swim::Render
