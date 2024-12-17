#pragma once

#include "Vertex.h"
#include "MeshBufferData.h"

namespace Engine
{

  struct Mesh
  {
    std::vector<Vertex> vertices; 
    std::vector<uint16_t> indices; 

    std::shared_ptr<MeshBufferData> meshBufferData;

    Mesh() = default;

    Mesh(std::vector<Vertex> v, std::vector<uint16_t> i)
      : vertices(std::move(v)), indices(std::move(i))
    {}
  };

} 
