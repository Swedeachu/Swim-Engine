#include "Engine/Systems/Scene/Picking/ScenePicking.h"

#include "Engine/Components/Pickable.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Physics/RigidBody.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Engine::ScenePicking
{
	std::vector<PickHit> PickAll(const entt::registry& registry, const Ray& inputRay, const PickOptions& options)
	{
		std::vector<PickHit> hits;
		const float length = glm::length(inputRay.Direction);
		if (!(length > 0.0f))
		{
			return hits;
		}
		const Ray ray{ inputRay.Origin, inputRay.Direction / length };
		const auto consider = [&](entt::entity entity, const std::optional<RayHit>& hit)
		{
			if (!hit || hit->T > options.MaxDistance)
			{
				return;
			}
			const auto existing = std::find_if(hits.begin(), hits.end(),
				[entity](const PickHit& h)
				{
					return h.Entity == entity;
				});
			if (existing != hits.end())
			{
				if (hit->T < existing->Distance)
				{
					*existing = { entity, hit->T, hit->Point, hit->Normal };
				}
				return;
			}
			hits.push_back({ entity, hit->T, hit->Point, hit->Normal });
		};
		if (options.Pickables)
		{
			for (auto [entity, pickable, transform] : registry.view<Pickable, Transform>().each())
			{
				if (!pickable.Enabled || (pickable.Layers & options.LayerMask) == 0u || (options.Filter && !options.Filter(entity)))
				{
					continue;
				}
				const glm::mat4 world = glm::translate(transform.GetWorldMatrix(registry), pickable.Offset);
				consider(entity, pickable.Shape == PickShape::Sphere ? RayQueries::TransformedSphere(ray, world, pickable.Radius)
																	 : RayQueries::Box(ray, world, pickable.HalfExtents));
			}
		}
		if (options.Colliders && (options.LayerMask & 1u) != 0u)
		{
			for (auto [entity, body, transform] : registry.view<Rigidbody, Transform>().each())
			{
				if (registry.all_of<Pickable>(entity) || (options.Filter && !options.Filter(entity)))
				{
					continue; // A Pickable replaces the collider shape.
				}
				const glm::mat4& world = transform.GetWorldMatrix(registry);
				const auto& collider = body.collider;
				switch (collider.type)
				{
				case ColliderType::Sphere: consider(entity, RayQueries::TransformedSphere(ray, world, collider.sphere.radius)); break;
				case ColliderType::Capsule:
					consider(entity, RayQueries::TransformedCapsule(ray, world, collider.capsule.radius, collider.capsule.halfHeight));
					break;
				case ColliderType::Box: consider(entity, RayQueries::Box(ray, world, collider.box.halfExtents)); break;
				default: break;
				}
			}
		}
		std::sort(hits.begin(), hits.end(),
			[](const PickHit& a, const PickHit& b)
			{
				return a.Distance < b.Distance;
			});
		return hits;
	}

	std::optional<PickHit> PickClosest(const entt::registry& registry, const Ray& ray, const PickOptions& options)
	{
		const auto hits = PickAll(registry, ray, options);
		if (hits.empty())
		{
			return std::nullopt;
		}
		return hits.front();
	}

	std::optional<PickHit> PickAtScreen(
		const entt::registry& registry, const CameraSystem& cameras, float x, float y, const PickOptions& options)
	{
		return PickClosest(registry, cameras.ScreenPointToRay(x, y), options);
	}
} // namespace Engine::ScenePicking
