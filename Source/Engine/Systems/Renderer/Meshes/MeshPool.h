#pragma once

#include <mutex>
#include "Mesh.h"
#include "Vertex.h"

namespace Engine
{

  class MeshPool
  {

  public:

    // Singleton accessor
    static MeshPool& GetInstance();

    // Delete copy and move constructors
    MeshPool(const MeshPool&) = delete;
    MeshPool& operator=(const MeshPool&) = delete;
    MeshPool(MeshPool&&) = delete;
    MeshPool& operator=(MeshPool&&) = delete;

    // Registers a mesh with a unique name. Returns the existing mesh if it already exists.
    std::shared_ptr<Mesh> RegisterMesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);

    // Retrieves a mesh by name
    std::shared_ptr<Mesh> GetMesh(const std::string& name) const;

    // Removes a mesh by name. Returns true if successful.
    bool RemoveMesh(const std::string& name);

    // Frees everything
    void Flush();

  private:

    // Private constructor for Singleton pattern
    MeshPool() = default;

    mutable std::mutex poolMutex; // Protects the mesh map
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;

  };

}
