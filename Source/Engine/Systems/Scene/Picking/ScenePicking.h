#pragma once

#include "Engine/Math/Ray.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace Engine
{

	class CameraSystem;
	class Scene;

	// One entity a picking ray hit.
	struct PickHit
	{
		entt::entity Entity = entt::null;
		float Distance = 0.0f;	  // Along the (normalized) ray.
		glm::vec3 Point{ 0.0f };  // World hit position.
		glm::vec3 Normal{ 0.0f }; // World surface normal, facing the ray.
	};

	struct PickOptions
	{
		float MaxDistance = 10000.0f;
		bool Pickables = true; // Test Pickable components.
		bool Colliders = true; // Test Rigidbody collider shapes (sphere, box, capsule).
		std::uint32_t LayerMask = 0xffffffffu; // Pickable::Layers & mask must be non-zero (colliders count as layer 1).
		std::function<bool(entt::entity)> Filter; // Optional: false skips an entity.
	};

	// Entity picking: rays against the entities' shapes in their world transforms (exact
	// for scaled, rotated boxes, spheres and capsules). Pure queries over the registry - no
	// physics world or renderer needed, so they work while paused, in tools and in tests.
	namespace ScenePicking
	{

		// Every hit (one per entity, its nearest), nearest first.
		std::vector<PickHit> PickAll(const entt::registry& registry, const Ray& ray, const PickOptions& options = {});
		// The nearest hit.
		std::optional<PickHit> PickClosest(const entt::registry& registry, const Ray& ray, const PickOptions& options = {});
		// The ray through a pixel of the render surface (top-left origin), then PickClosest.
		std::optional<PickHit> PickAtScreen(
			const entt::registry& registry, const CameraSystem& cameras, float x, float y, const PickOptions& options = {});

	} // namespace ScenePicking

} // namespace Engine
