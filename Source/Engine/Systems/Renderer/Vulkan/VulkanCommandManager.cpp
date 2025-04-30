#include "PCH.h"
#include "VulkanCommandManager.h"
#include <stdexcept>

namespace Engine
{

	VulkanCommandManager::VulkanCommandManager(VkDevice device, uint32_t graphicsQueueFamilyIndex)
		: device(device)
	{
		CreateCommandPool(graphicsQueueFamilyIndex);
	}

	VulkanCommandManager::~VulkanCommandManager()
	{
		Cleanup();
	}

	void VulkanCommandManager::CreateCommandPool(uint32_t queueFamilyIndex)
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndex;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create command pool!");
		}
	}

	void VulkanCommandManager::AllocateCommandBuffers(uint32_t count)
	{
		commandBuffers.resize(count);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = count;

		if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate command buffers!");
		}
	}

	const std::vector<VkCommandBuffer>& VulkanCommandManager::GetCommandBuffers() const
	{
		return commandBuffers;
	}

	VkCommandBuffer VulkanCommandManager::BeginSingleTimeCommands() const
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate single-time command buffer!");
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void VulkanCommandManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer, VkQueue queue) const
	{
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit single-time command buffer!");
		}

		vkQueueWaitIdle(queue);
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	}

	void VulkanCommandManager::Cleanup()
	{
		if (commandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device, commandPool, nullptr);
			commandPool = VK_NULL_HANDLE;
		}
	}

}
