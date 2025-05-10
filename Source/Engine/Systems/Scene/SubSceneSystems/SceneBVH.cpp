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

  SceneBVH::AABB SceneBVH::CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform)
  {
    glm::vec3 min = mesh->meshBufferData->aabbMin;
    glm::vec3 max = mesh->meshBufferData->aabbMax;
    glm::mat4 model = transform.GetModelMatrix();

    glm::vec3 corners[8] = {
        glm::vec3(model * glm::vec4(min.x, min.y, min.z, 1.0f)),
        glm::vec3(model * glm::vec4(max.x, min.y, min.z, 1.0f)),
        glm::vec3(model * glm::vec4(min.x, max.y, min.z, 1.0f)),
        glm::vec3(model * glm::vec4(max.x, max.y, min.z, 1.0f)),
        glm::vec3(model * glm::vec4(min.x, min.y, max.z, 1.0f)),
        glm::vec3(model * glm::vec4(max.x, min.y, max.z, 1.0f)),
        glm::vec3(model * glm::vec4(min.x, max.y, max.z, 1.0f)),
        glm::vec3(model * glm::vec4(max.x, max.y, max.z, 1.0f))
    };

    glm::vec3 worldMin = corners[0];
    glm::vec3 worldMax = corners[0];

    for (int i = 1; i < 8; ++i)
    {
      worldMin = glm::min(worldMin, corners[i]);
      worldMax = glm::max(worldMax, corners[i]);
    }

    return { worldMin, worldMax };
  }

  void SceneBVH::UpdateIfNeeded(entt::observer& frustumObserver)
  {
    bool needsUpdate = false;

    for (auto entity : frustumObserver)
    {
      needsUpdate = true;
      break;
    }

    if (!needsUpdate && Transform::AreAnyTransformsDirty())
    {
      needsUpdate = true;
    }

    if (needsUpdate)
    {
      Update();
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

  bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
  {
    glm::vec3 corners[8] = {
        { aabb.min.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.min.y, aabb.min.z },
        { aabb.min.x, aabb.max.y, aabb.min.z },
        { aabb.max.x, aabb.max.y, aabb.min.z },
        { aabb.min.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.min.y, aabb.max.z },
        { aabb.min.x, aabb.max.y, aabb.max.z },
        { aabb.max.x, aabb.max.y, aabb.max.z }
    };

    for (int i = 0; i < 6; ++i)
    {
      int outside = 0;
      for (int j = 0; j < 8; ++j)
      {
        if (glm::dot(glm::vec3(frustum.planes[i]), corners[j]) + frustum.planes[i].w < 0.0f)
        {
          outside++;
        }
      }
      if (outside == 8)
      {
        return false;
      }
    }

    return true;
  }

}