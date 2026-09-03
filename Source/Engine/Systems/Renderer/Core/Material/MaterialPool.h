#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include "MaterialData.h"
#include <tiny_gltf.h>

namespace Engine
{

  class MeshPool;
  class TexturePool;

  class MaterialPool
  {

  public:

    using EditorMessageCallback = std::function<bool(const std::string&, std::uintptr_t)>;

    MaterialPool(MeshPool& meshes, TexturePool& textures, EditorMessageCallback sendEditorMessage = {})
      : meshes(&meshes), textures(&textures), sendEditorMessage(std::move(sendEditorMessage)) {}

    // Delete copy and move constructors
    MaterialPool(const MaterialPool&) = delete;
    MaterialPool& operator=(const MaterialPool&) = delete;
    MaterialPool(MaterialPool&&) = delete;
    MaterialPool& operator=(MaterialPool&&) = delete;

    // Retrieves or creates a MaterialData
    std::shared_ptr<MaterialData> GetMaterialData(const std::string& name);
    std::shared_ptr<MaterialData> GetMaterialDataByID(uint32_t id);
    std::string GetMaterialNameByID(uint32_t id);
    std::shared_ptr<MaterialData> RegisterMaterialData(const std::string& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture2D> albedoMap = nullptr);
    bool MaterialExists(const std::string& name);

    // Load a GLB file from disk, this will be used for making a composite material (vector of materials)
    std::vector<std::shared_ptr<MaterialData>> LoadAndRegisterCompositeMaterialFromGLB(const std::string& path);
    std::vector<std::shared_ptr<MaterialData>> GetCompositeMaterialData(const std::string& name);
    std::vector<std::shared_ptr<MaterialData>> LazyLoadAndGetCompositeMaterial(const std::string& path);
    bool CompositeMaterialExists(const std::string& name);
    

    // Frees all 
    void Flush();

  private:

    MeshPool* meshes = nullptr;
    TexturePool* textures = nullptr;
    EditorMessageCallback sendEditorMessage;

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
