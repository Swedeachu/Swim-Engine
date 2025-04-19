#include "PCH.h"
#include "MaterialDescriptor.h"
#include "Engine/Systems/Renderer/VulkanRenderer.h" 

namespace Engine
{

  MaterialDescriptor::MaterialDescriptor(VulkanRenderer& vulkanRenderer, const std::shared_ptr<Texture2D>& texture)
  {
    descriptorSet = vulkanRenderer.CreateMaterialDescriptorSet(texture);
  }

  MaterialDescriptor::~MaterialDescriptor()
  {
    // Descriptor sets are managed by the vulkanRenderer's pool
  }

}
