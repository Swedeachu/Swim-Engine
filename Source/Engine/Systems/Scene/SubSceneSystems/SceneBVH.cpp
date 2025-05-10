#include "PCH.h"
#include "SceneBVH.h"
#include "Engine/Components/Material.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

namespace Engine
{

	SceneBVH::SceneBVH(entt::registry& registry)
		: registry(registry)
	{}

	void SceneBVH::Init()
	{
		observer.connect(registry, entt::collector
			.group<Transform, Material>()
			.update<Transform>()
			.update<Material>()
		);

		registry.on_destroy<Transform>().connect<&SceneBVH::RemoveEntity>(*this);
		registry.on_destroy<Material>().connect<&SceneBVH::RemoveEntity>(*this);
	}

	// Macro to expand a corner and update min/max
#define EXPAND_CORNER(x, y, z)                                                        \
{                                                                                     \
    glm::vec3 corner = glm::vec3(model * glm::vec4(x, y, z, 1.0f));                   \
    worldMin = glm::min(worldMin, corner);                                            \
    worldMax = glm::max(worldMax, corner);                                            \
}

	SceneBVH::AABB SceneBVH::CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform)
	{
		glm::vec3 min = mesh->meshBufferData->aabbMin;
		glm::vec3 max = mesh->meshBufferData->aabbMax;
		glm::mat4 model = transform.GetModelMatrix();

		// Start with first corner (so we can initialize it first)
		glm::vec3 worldMin = glm::vec3(model * glm::vec4(min.x, min.y, min.z, 1.0f));
		glm::vec3 worldMax = worldMin;

		// Unroll remaining 7 corners
		EXPAND_CORNER(max.x, min.y, min.z);
		EXPAND_CORNER(min.x, max.y, min.z);
		EXPAND_CORNER(max.x, max.y, min.z);
		EXPAND_CORNER(min.x, min.y, max.z);
		EXPAND_CORNER(max.x, min.y, max.z);
		EXPAND_CORNER(min.x, max.y, max.z);
		EXPAND_CORNER(max.x, max.y, max.z);

		return { worldMin, worldMax };
	}

#undef EXPAND_CORNER

	void SceneBVH::UpdateIfNeeded(entt::observer& frustumObserver)
	{
		bool needsUpdate = frustumObserver.empty();

		if (!needsUpdate && Transform::AreAnyTransformsDirty())
		{
			needsUpdate = true;
		}

		if (needsUpdate)
		{
			Update();
		}
	}

	void SceneBVH::DebugRender()
	{
		if (debugDrawer != nullptr && debugDrawer->IsEnabled())
		{
			for (const auto& node : nodes)
			{
				const glm::vec3& min = node.aabb.min;
				const glm::vec3& max = node.aabb.max;

				// Submit a wireframe box for each BVH node
				debugDrawer->SubmitWireframeBoxAABB(min, max);
			}
		}
	}

	void SceneBVH::Update()
	{
		auto view = registry.view<Transform, Material>();

		for (auto entity : view)
		{
			const auto& transform = view.get<Transform>(entity);
			const auto& mat = view.get<Material>(entity).data;
			const auto& mesh = mat->mesh;

			auto it = entityToNodeIndex.find(entity);
			if (it != entityToNodeIndex.end())
			{
				// existing update if dirty
				if (transform.IsDirty())
				{
					nodes[it->second].aabb = CalculateWorldAABB(mesh, transform);
				}
			}
			else
			{
				// new insert
				SceneBVH::AABB aabb = CalculateWorldAABB(mesh, transform);
				nodes.emplace_back(entity, aabb);
				entityToNodeIndex[entity] = nodes.size() - 1;
			}
		}

		observer.clear();
	}

	void SceneBVH::QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const
	{
		outVisible.clear();
		outVisible.reserve(nodes.size());

		for (const auto& node : nodes)
		{
			if (IsAABBVisible(frustum, node.aabb))
			{
				outVisible.push_back(node.entity);
			}
		}
	}

	void SceneBVH::RemoveEntity(entt::entity entity)
	{
		auto it = entityToNodeIndex.find(entity);
		if (it != entityToNodeIndex.end())
		{
			size_t index = it->second;
			size_t last = nodes.size() - 1;

			if (index != last)
			{
				std::swap(nodes[index], nodes[last]);
				entityToNodeIndex[nodes[index].entity] = index;
			}

			nodes.pop_back();
			entityToNodeIndex.erase(it);
		}
	}

	// Macro for testing a single corner against a plane, we do this so we don't have to declare an array of corners each time
#define TEST_CORNER(plane, x, y, z) \
    (glm::dot(glm::vec3(plane), glm::vec3(x, y, z)) + (plane).w < 0.0f ? 1 : 0)

	// IsAABBVisible fully unrolled version
	bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
	{
		// Unroll for all 6 planes, we still keep this loop though because the compiler is best at optimizing regular fixed size for loops
		for (int i = 0; i < 6; i++)
		{
			const glm::vec4& plane = frustum.planes[i];
			int outside = 0;

			// Manually test all 8 corners
			outside += TEST_CORNER(plane, aabb.min.x, aabb.min.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.min.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.min.x, aabb.max.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.max.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.min.x, aabb.min.y, aabb.max.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.min.y, aabb.max.z);
			outside += TEST_CORNER(plane, aabb.min.x, aabb.max.y, aabb.max.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.max.y, aabb.max.z);

			// If all corners are outside of this plane, the box is not visible
			if (outside == 8)
			{
				return false;
			}
		}

		// Otherwise, box intersects or is inside the frustum
		return true;
	}

#undef TEST_CORNER

}
