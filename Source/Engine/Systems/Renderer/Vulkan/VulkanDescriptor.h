#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

  // Forward declaration of VulkanRenderer
  class VulkanRenderer;

  struct VulkanDescriptor
  {

    VkDescriptorSet descriptorSet;

    VulkanDescriptor(VulkanRenderer& vulkanRenderer, const std::shared_ptr<Texture2D>& texture);

    ~VulkanDescriptor();

  };

}
