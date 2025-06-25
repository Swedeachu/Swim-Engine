#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include "MaterialData.h"
#include "Library/tiny_gltf/tiny_gltf.h"

namespace Engine
{

  class MaterialPool
  {

  public:

    // Singleton accessor
    static MaterialPool& GetInstance();

    // Delete copy and move constructors
    MaterialPool(const MaterialPool&) = delete;
    MaterialPool& operator=(const MaterialPool&) = delete;
    MaterialPool(MaterialPool&&) = delete;
    MaterialPool& operator=(MaterialPool&&) = delete;

    // Retrieves or creates a MaterialData
    std::shared_ptr<MaterialData> GetMaterialData(const std::string& name);
    std::shared_ptr<MaterialData> RegisterMaterialData(const std::string& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture2D> albedoMap = nullptr);
    bool MaterialExists(const std::string& name);

    // Load a GLB file from disk, this will be used for making a composite material (vector of materials)
    std::vector<std::shared_ptr<MaterialData>> LoadAndRegisterCompositeMaterialFromGLB(const std::string& path);
    std::vector<std::shared_ptr<MaterialData>> GetCompositeMaterialData(const std::string& name);
    bool CompositeMaterialExists(const std::string& name);

    // Frees all 
    void Flush();

  private:

    MaterialPool() = default;

    void LoadNodeRecursive
    (
      const tinygltf::Model& model,
      int nodeIndex,
      const glm::mat4& parentTransform,
      const std::string& path,
      std::vector<std::shared_ptr<MaterialData>>& loadedMaterials
    );

    mutable std::mutex poolMutex;
    std::unordered_map<std::string, std::shared_ptr<MaterialData>> materials;
    std::unordered_map<std::string, std::vector<std::shared_ptr<MaterialData>>> compositeMaterials;

  };

}
