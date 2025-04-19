#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include "MaterialDescriptor.h"

namespace Engine
{

  class DescriptorPool
  {

  public:

    // Singleton accessor
    static DescriptorPool& GetInstance();

    // Delete copy and move constructors
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    DescriptorPool(DescriptorPool&&) = delete;
    DescriptorPool& operator=(DescriptorPool&&) = delete;

    // Retrieves or creates a MaterialDescriptor
    std::shared_ptr<MaterialDescriptor> GetMaterialDescriptor(VulkanRenderer& vulkanRenderer, const std::shared_ptr<Texture2D>& albedoMap);

    // Frees all MaterialDescriptors
    void Flush();

  private:

    DescriptorPool() = default;

    mutable std::mutex poolMutex;
    std::unordered_map<std::shared_ptr<Texture2D>, std::shared_ptr<MaterialDescriptor>> descriptors;

  };

}
