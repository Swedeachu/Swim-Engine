#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

  // Forward declaration of VulkanRenderer
  class VulkanRenderer;

  // TODO this struct/file needs to be renamed completely as this is a purely vulkan specific type required for textures
  struct VulkanDescriptor
  {

    VkDescriptorSet descriptorSet;

    VulkanDescriptor(VulkanRenderer& vulkanRenderer, const std::shared_ptr<Texture2D>& texture);

    ~VulkanDescriptor();

  };

}
