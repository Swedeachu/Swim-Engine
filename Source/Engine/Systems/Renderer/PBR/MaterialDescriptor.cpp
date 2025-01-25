#include "PCH.h"
#include "MaterialDescriptor.h"
#include "Engine/Systems/Renderer/VulkanRenderer.h" 

namespace Engine
{

  MaterialDescriptor::MaterialDescriptor(VulkanRenderer& renderer, const std::shared_ptr<Texture2D>& texture)
  {
    descriptorSet = renderer.CreateMaterialDescriptorSet(texture);
  }

  MaterialDescriptor::~MaterialDescriptor()
  {
    // Descriptor sets are managed by the renderer's pool
  }

}
