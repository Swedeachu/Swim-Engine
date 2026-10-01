#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Engine
{

  class VulkanCommandManager
  {

  public:

    VulkanCommandManager(VkDevice device, uint32_t graphicsQueueFamilyIndex);
    ~VulkanCommandManager();

    void AllocateCommandBuffers(uint32_t count);
    const std::vector<VkCommandBuffer>& GetCommandBuffers() const;

    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer, VkQueue queue) const;

    VkCommandPool GetCommandPool() const { return commandPool; }

    void Cleanup();

  private:

    VkDevice device;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    void CreateCommandPool(uint32_t queueFamilyIndex);

  };

}
