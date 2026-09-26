#include "PCH.h"

#include "VulkanSyncManager.h"
#include <stdexcept>

namespace Engine
{

	VulkanSyncManager::VulkanSyncManager(VkDevice device, size_t maxFramesInFlight)
		: device(device), maxFramesInFlight(maxFramesInFlight)
	{
		Init();
	}

	void VulkanSyncManager::Init()
	{
		imageAvailableSemaphores.resize(maxFramesInFlight);
		renderFinishedSemaphores.resize(maxFramesInFlight);
		inFlightFences.resize(maxFramesInFlight);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < maxFramesInFlight; ++i)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create synchronization objects for a frame.");
			}
		}
	}

	VulkanSyncManager::~VulkanSyncManager()
	{
		Cleanup();
	}

	VkSemaphore VulkanSyncManager::GetImageAvailableSemaphore(size_t frameIndex) const
	{
		return imageAvailableSemaphores[frameIndex];
	}

	VkSemaphore VulkanSyncManager::GetRenderFinishedSemaphore(size_t frameIndex) const
	{
		return renderFinishedSemaphores[frameIndex];
	}

	VkFence VulkanSyncManager::GetInFlightFence(size_t frameIndex) const
	{
		return inFlightFences[frameIndex];
	}

	void VulkanSyncManager::WaitForFence(size_t frameIndex) const
	{
		if (vkWaitForFences(device, 1, &inFlightFences[frameIndex], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to wait for in-flight fence!");
		}
	}

	void VulkanSyncManager::ResetFence(size_t frameIndex) const
	{
		if (vkResetFences(device, 1, &inFlightFences[frameIndex]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to reset in-flight fence!");
		}
	}

	void VulkanSyncManager::Cleanup()
	{
		for (size_t i = 0; i < maxFramesInFlight; ++i)
		{
			if (imageAvailableSemaphores[i])
			{
				vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
				imageAvailableSemaphores[i] = VK_NULL_HANDLE;
			}
			if (renderFinishedSemaphores[i])
			{
				vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
				renderFinishedSemaphores[i] = VK_NULL_HANDLE;
			}
			if (inFlightFences[i])
			{
				vkDestroyFence(device, inFlightFences[i], nullptr);
				inFlightFences[i] = VK_NULL_HANDLE;
			}
		}
	}

}
