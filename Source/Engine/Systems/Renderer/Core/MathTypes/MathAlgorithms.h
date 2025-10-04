#pragma once

#include "Ray.h"
#include "AABB.h"

namespace Engine
{

  bool RayIntersectsAABB
  (
    const Ray& ray,
    const AABB& box,
    float tMin, 
    float tMax,
    float& outTNear
  );

  glm::quat FromToRotation(const glm::vec3& from, const glm::vec3& to);

}
