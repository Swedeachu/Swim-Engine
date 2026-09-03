#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include "LegacyRenderBinding.h"
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

    // Retrieves or creates a transitional legacy draw binding.
    std::shared_ptr<LegacyRenderBinding> GetMaterialBinding(const std::string& name);
    std::shared_ptr<LegacyRenderBinding> GetMaterialBindingByID(uint32_t id);
    std::string GetMaterialNameByID(uint32_t id);
    std::shared_ptr<LegacyRenderBinding> RegisterMaterialBinding(const std::string& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture2D> albedoMap = nullptr);
    bool MaterialExists(const std::string& name);

    // Load a GLB file from disk, this will be used for making a composite material (vector of materials)
    std::vector<std::shared_ptr<LegacyRenderBinding>> LoadAndRegisterCompositeMaterialFromGLB(const std::string& path);
    std::vector<std::shared_ptr<LegacyRenderBinding>> GetCompositeMaterialData(const std::string& name);
    std::vector<std::shared_ptr<LegacyRenderBinding>> LazyLoadAndGetCompositeMaterial(const std::string& path);
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
      std::vector<std::shared_ptr<LegacyRenderBinding>>& loadedMaterials
    );

    mutable std::mutex poolMutex;
    std::unordered_map<std::string, std::shared_ptr<LegacyRenderBinding>> materials;
    std::unordered_map<std::string, std::vector<std::shared_ptr<LegacyRenderBinding>>> compositeMaterials;

  };

}
