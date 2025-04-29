#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include "MaterialData.h"

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

    // Frees all 
    void Flush();

  private:

    MaterialPool() = default;

    mutable std::mutex poolMutex;
    std::unordered_map<std::string, std::shared_ptr<MaterialData>> materials;

  };

}
