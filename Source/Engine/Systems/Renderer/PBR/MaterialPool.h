#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include "MaterialDescriptor.h"

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

    // Retrieves or creates a MaterialDescriptor
    std::shared_ptr<MaterialDescriptor> GetMaterialDescriptor(VulkanRenderer& renderer, const std::shared_ptr<Texture2D>& albedoMap);

    // Frees all MaterialDescriptors
    void Flush();

  private:

    MaterialPool() = default;

    mutable std::mutex poolMutex;
    std::unordered_map<std::shared_ptr<Texture2D>, std::shared_ptr<MaterialDescriptor>> descriptors;

  };

}
