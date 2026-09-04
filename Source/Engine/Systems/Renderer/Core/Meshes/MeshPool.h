#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "Mesh.h"
#include "MeshBufferData.h"
#include "Vertex.h"
#include "PrimitiveMeshes.h"
#include "Engine/Assets/ContentHash.h"

namespace Engine
{

  class Renderer;

  class MeshPool
  {

  public:

    explicit MeshPool(Renderer& renderer) : renderer(&renderer) {}

    // Delete copy and move constructors
    MeshPool(const MeshPool&) = delete;
    MeshPool& operator=(const MeshPool&) = delete;
    MeshPool(MeshPool&&) = delete;
    MeshPool& operator=(MeshPool&&) = delete;

    // Registers a mesh with a unique name. Returns the existing mesh if it already exists.
    std::shared_ptr<Mesh> RegisterMesh(const std::string& name, const VertexesIndexesPair& data);
    std::shared_ptr<Mesh> RegisterMesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // If you care about registering meshes super quick, don't use this. This is for safety.
    std::shared_ptr<Mesh> GetOrCreateAndRegisterMesh(const std::string& desiredName, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Retrieves a mesh by name
    std::shared_ptr<Mesh> GetMesh(const std::string& name) const;

    uint32_t GetMeshID(const std::shared_ptr<Mesh>& mesh) const;
    std::shared_ptr<Mesh> GetMeshByID(uint32_t id) const;
    std::shared_ptr<MeshBufferData> GetMeshBufferData(const std::shared_ptr<Mesh>& mesh) const;

    // Explicit compatibility residency request. RegisterMesh() only creates CPU geometry.
    std::shared_ptr<MeshBufferData> RequestMeshResidency(const std::shared_ptr<Mesh>& mesh);

    // Removes a mesh by name. Returns true if successful.
    bool RemoveMesh(const std::string& name);

    // Frees everything
    void Flush();

  private:

    Renderer* renderer = nullptr;

    mutable std::mutex poolMutex; // Protects the mesh map
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;

    // Maps for mesh indexing
    std::unordered_map<std::shared_ptr<Mesh>, uint32_t> meshToID;
    std::unordered_map<uint32_t, std::shared_ptr<Mesh>> idToMesh;
    std::unordered_map<std::shared_ptr<Mesh>, std::shared_ptr<MeshBufferData>> meshResidency;
    std::unordered_map<Swim::Assets::ContentHash, std::weak_ptr<Mesh>> meshContentIndex;
    uint32_t nextMeshID = 0; 

  };

}
