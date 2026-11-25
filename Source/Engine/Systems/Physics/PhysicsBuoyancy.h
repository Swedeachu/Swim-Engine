#pragma once

#include "PhysicsTypes.h"

#include <cstdint>
#include <functional>
#include <limits>

namespace Engine
{

	// The fluid at one world column: where its surface is and how it flows.
	struct FluidSample
	{
		float SurfaceHeight = 0.0f;
		glm::vec3 FlowVelocity{ 0.0f }; // m/s; drag pulls bodies toward it.
	};

	// World x, z and the world's simulated seconds (PhysicsWorld::GetSimulationTime) -> the
	// fluid there. Called on the thread that calls BeginSimulation, a few times per buoyant
	// body per step.
	using FluidSurfaceFunction = std::function<FluidSample(float x, float z, double time)>;

	// A body of liquid: everything inside Bounds and below the surface.
	struct FluidVolumeDesc
	{
		FluidSurfaceFunction Surface; // Empty: flat and still at SurfaceHeight.
		float SurfaceHeight = 0.0f;
		float Density = 1000.0f;				 // kg/m^3 (fresh water 1000).
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f }; // Buoyancy pushes against it (match the world's).
		// Drag rates (1/s) for a body displacing its own mass (floating at rest), relative to the
		// flow; scaled by displaced mass / body mass, so heavy bodies and bodies barely in the
		// fluid slow less, and light floaters settle as quickly as dense ones.
		float LinearDrag = 1.5f;
		float AngularDrag = 1.5f;
		glm::vec3 BoundsMin{ -std::numeric_limits<float>::max() };
		glm::vec3 BoundsMax{ std::numeric_limits<float>::max() };
	};

	// What a body displaces. The volume is split over a Samples^3 grid of points across the
	// box (body space); each point is a slab of the volume, submerged in proportion to its
	// depth, and pushes up at its own position, so a tilted body is righted and a body
	// floats where the displaced fluid weighs what it weighs.
	struct BuoyancyDesc
	{
		float Volume = 1.0f; // m^3 displaced when fully submerged.
		glm::vec3 Center{ 0.0f };
		glm::vec3 HalfExtents{ 0.5f };
		std::uint32_t Samples = 2; // Per axis, 1..4.
		float DragScale = 1.0f;	   // Multiplies the fluid's drag (flat or streamlined bodies).
		// kg/m^3. Above 0, lift is scaled so the body floats as if this dense whatever its mass
		// (submerged fraction at rest = Density / fluid density). 0: its own mass decides.
		float Density = 0.0f;
		// Extra damping (1/s, at floating rest) of vertical speed relative to the surface, so
		// floaters settle on waves instead of bobbing through them.
		float HeaveDamping = 0.0f;
		// After every step, a body whose top (of the box) is below the surface is lifted until
		// it is TopMargin above it, and stops moving down faster than the surface.
		bool NeverSink = false;
		float TopMargin = 0.0f;

		static BuoyancyDesc Box(const glm::vec3& halfExtents);

		// A light floater for props and menus: a box half as dense as water, damped, never under.
		static BuoyancyDesc Floater(const glm::vec3& halfExtents, float density = 500.0f);

		static BuoyancyDesc Sphere(float radius);

		static BuoyancyDesc Capsule(float radius, float halfHeight); // Along body Y.
	};

	struct BuoyancyState
	{
		float Submerged = 0.0f;	  // 0..1 of the volume, last step.
		glm::vec3 Force{ 0.0f };  // Buoyant force applied last step (N).
		std::int32_t Volume = -1; // Index of the fluid volume it was in (-1: none).
	};

	struct FluidVolumeHandle
	{
		std::uint32_t Id = 0; // 0: invalid.

		explicit operator bool() const { return Id != 0; }

		auto operator<=>(const FluidVolumeHandle&) const = default;
	};

} // namespace Engine
