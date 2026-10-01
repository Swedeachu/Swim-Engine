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

  bool ClosestParamsTwoLines
  (
    const glm::vec3& p0, const glm::vec3& u,
    const glm::vec3& q0, const glm::vec3& v,
    float& outT, float& outS
  );

  float ParamOnAxisFromRay
  (
    const glm::vec3& axisOrigin,
    const glm::vec3& axisDirN,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirN
  );

}
