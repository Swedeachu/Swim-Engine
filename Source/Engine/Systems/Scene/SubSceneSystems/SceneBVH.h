#pragma once

#include "Library/glm/glm.hpp"
#include "Library/EnTT/entt.hpp"

namespace Engine
{

  class Transform;
  class Mesh;
  struct Frustum;

  class SceneBVH
  {

  public:

    struct AABB
    {
      glm::vec3 min;
      glm::vec3 max;
    };

    SceneBVH(entt::registry& registry);

    void Init();
    void Update();
    void UpdateIfNeeded(entt::observer& frustumObserver);
    void QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const;
    void RemoveEntity(entt::entity entity);

  private:

    struct BVHNode
    {
      entt::entity entity;
      AABB aabb;

      BVHNode(entt::entity e, const AABB& box)
        : entity(e), aabb(box)
      {}
    };

    entt::registry& registry;
    entt::observer observer;

    std::vector<BVHNode> nodes;
    std::unordered_map<entt::entity, size_t> entityToNodeIndex;

    bool IsAABBVisible(const Frustum& frustum, const AABB& aabb) const;
    SceneBVH::AABB CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform);

  };

}
