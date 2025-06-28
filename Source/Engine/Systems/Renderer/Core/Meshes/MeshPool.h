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
    std::shared_ptr<Mesh> RegisterMesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // If you care about registering meshes super quick, don't use this. This is for safety.
    std::shared_ptr<Mesh> GetOrCreateAndRegisterMesh(const std::string& desiredName, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Retrieves a mesh by name
    std::shared_ptr<Mesh> GetMesh(const std::string& name) const;

    uint32_t GetMeshID(const std::shared_ptr<Mesh>& mesh) const;
    std::shared_ptr<Mesh> GetMeshByID(uint32_t id) const;

    // Removes a mesh by name. Returns true if successful.
    bool RemoveMesh(const std::string& name);

    // Frees everything
    void Flush();

  private:

    // Private constructor for Singleton pattern
    MeshPool() = default;

    mutable std::mutex poolMutex; // Protects the mesh map
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;

    // Vulkan context cache
    bool vulkanDevicesCached = false;
    VkDevice cachedDevice = VK_NULL_HANDLE;
    VkPhysicalDevice cachedPhysicalDevice = VK_NULL_HANDLE;

    // Maps for mesh indexing
    std::unordered_map<std::shared_ptr<Mesh>, uint32_t> meshToID;
    std::unordered_map<uint32_t, std::shared_ptr<Mesh>> idToMesh;
    uint32_t nextMeshID = 0; 

  };

}
