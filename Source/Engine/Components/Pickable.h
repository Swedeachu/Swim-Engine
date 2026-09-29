#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Engine
{

	enum class PickShape : std::uint8_t
	{
		Sphere, // Radius, around Offset.
		Box,	// HalfExtents, around Offset.
	};

	// Makes an entity selectable by ScenePicking with an explicit shape in the entity's local
	// space (it follows the world Transform, scale included). Entities with a Rigidbody are
	// pickable by their collider without one; a Pickable overrides the collider shape.
	struct Pickable
	{
		bool Enabled = true;
		PickShape Shape = PickShape::Sphere;
		float Radius = 0.5f;
		glm::vec3 HalfExtents{ 0.5f };
		glm::vec3 Offset{ 0.0f };
		std::uint32_t Layers = 1u; // Matched against PickOptions::LayerMask.
	};

} // namespace Engine
