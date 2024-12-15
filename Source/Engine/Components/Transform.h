#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

  // TO DO: rotation with quaternion and matrix with dirty flag to recompute the matrix when needed to retreive it
  // we should make glm do as much of that math as possible ofc
  struct Transform
  {
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

    Transform() = default;
    Transform(const glm::vec3& pos, const glm::vec3& scl)
      : position(pos), scale(scl)
    {}
  };

}