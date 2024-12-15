#pragma once

#include "Engine/Systems/Renderer/Buffer/Vertex.h"

namespace Engine
{

  struct Mesh
  {
    std::vector<Vertex> vertices;  // Vertices of the mesh
    std::vector<uint16_t> indices; // Indices for drawing

    Mesh() = default;

    Mesh(std::vector<Vertex> v, std::vector<uint16_t> i)
      : vertices(std::move(v)), indices(std::move(i))
    {}
  };

} // namespace Engine
