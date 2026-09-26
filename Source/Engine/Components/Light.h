#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Engine
{
	enum class LightKind : std::uint8_t
	{
		Directional, // Everywhere, along the entity's forward (-Z).
		Point,		 // From the entity's position, bounded by Range.
		Spot,		 // From the entity's position along its forward, within OuterCone.
	};

	// A punctual light on an entity (Phase 23). Position and direction come from the
	// entity's world Transform (forward is local -Z). The render bridge mirrors every
	// Light into the GPU light buffer each frame; clustered Forward+ shades with all of
	// them and the shadow planner gives the CastShadows ones shadow maps (cascades for
	// directional, one cone view for spots, a cube for points) within the budgets.
	struct Light
	{
		LightKind Kind = LightKind::Point;
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f }; // Linear.
		// Candela for point/spot lights, lux for directional ones.
		float Intensity = 10.0f;
		float Range = 10.0f;	 // Point/spot: where the light reaches zero.
		float InnerCone = 0.35f; // Spot, radians (full falloff starts here).
		float OuterCone = 0.6f;	 // Spot, radians, <= pi / 2.
		bool CastShadows = false;
		float ShadowPriority = 0.0f; // Larger wins the shadow budgets and atlas space.
		bool Enabled = true;
	};
} // namespace Engine
