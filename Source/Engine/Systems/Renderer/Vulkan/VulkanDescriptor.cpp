#include "PCH.h"
#include "VulkanDescriptor.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h" 

namespace Engine
{

  VulkanDescriptor::VulkanDescriptor(VulkanRenderer& vulkanRenderer, const std::shared_ptr<Texture2D>& texture)
  {
    descriptorSet = vulkanRenderer.CreateDescriptorSet(texture); 
  }

  VulkanDescriptor::~VulkanDescriptor()
  {
    // Descriptor sets are managed by the vulkanRenderer's pool
  }

}
