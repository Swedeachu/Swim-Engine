#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "Engine/Systems/Renderer/Textures/Texture2D.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

  // Forward declaration of VulkanRenderer
  class VulkanRenderer;

  struct MaterialDescriptor
  {

    VkDescriptorSet descriptorSet;

    MaterialDescriptor(VulkanRenderer& renderer, const std::shared_ptr<Texture2D>& texture);

    ~MaterialDescriptor();

  };

}
