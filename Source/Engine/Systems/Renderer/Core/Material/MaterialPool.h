#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "LegacyRenderBinding.h"
#include "Engine/Assets/ContentHash.h"
#include "Engine/Assets/AssetId.h"

namespace Swim::Assets
{
	class AssetSystem;
}

namespace Engine
{

  class MeshPool;
  class TexturePool;

  class MaterialPool
  {

  public:

    MaterialPool(Swim::Assets::AssetSystem& assets, MeshPool& meshes, TexturePool& textures)
      : assets(&assets), meshes(&meshes), textures(&textures) {}

    // Delete copy and move constructors
    MaterialPool(const MaterialPool&) = delete;
    MaterialPool& operator=(const MaterialPool&) = delete;
    MaterialPool(MaterialPool&&) = delete;
    MaterialPool& operator=(MaterialPool&&) = delete;

    // Retrieves or creates a transitional legacy draw binding.
    std::shared_ptr<LegacyRenderBinding> GetMaterialBinding(const std::string& name);
    std::shared_ptr<LegacyRenderBinding> GetMaterialBindingByID(uint32_t id);
    std::string GetMaterialNameByID(uint32_t id);
    std::shared_ptr<LegacyRenderBinding> RegisterMaterialBinding(
      const std::string& name,
      std::shared_ptr<Mesh> mesh,
      std::shared_ptr<Texture2D> albedoMap = nullptr,
      Swim::Assets::AssetId materialAssetId = {},
      Swim::Assets::AssetId meshAssetId = {});
    bool MaterialExists(const std::string& name);

    // Builds legacy renderer residency from the authoritative cooked ModelAsset.
    // The source path is only an authoring lookup key; no GLB/source import occurs here.
    std::vector<std::shared_ptr<LegacyRenderBinding>> LoadAndRegisterCompositeMaterial(const std::string& sourcePath);
    std::vector<std::shared_ptr<LegacyRenderBinding>> GetCompositeMaterialData(const std::string& name);
    std::vector<std::shared_ptr<LegacyRenderBinding>> LazyLoadAndGetCompositeMaterial(const std::string& sourcePath);
    bool CompositeMaterialExists(const std::string& name);
    Swim::Assets::AssetId GetCompositeMaterialAssetId(const std::string& sourcePath) const;

    // Frees all
    void Flush();

  private:

    Swim::Assets::AssetSystem* assets = nullptr;
    MeshPool* meshes = nullptr;
    TexturePool* textures = nullptr;

    mutable std::mutex poolMutex;
    std::unordered_map<std::string, std::shared_ptr<LegacyRenderBinding>> materials;
    std::unordered_map<std::string, std::vector<std::shared_ptr<LegacyRenderBinding>>> compositeMaterials;
    std::unordered_map<std::string, Swim::Assets::ContentHash> compositeMaterialRevisions;
    std::unordered_map<std::string, Swim::Assets::AssetId> compositeMaterialAssetIds;

  };

}
