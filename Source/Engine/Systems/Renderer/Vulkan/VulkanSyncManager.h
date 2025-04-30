#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Engine
{

	class VulkanSyncManager
	{

	public:

		VulkanSyncManager(VkDevice device, size_t maxFramesInFlight);
		~VulkanSyncManager();

		VkSemaphore GetImageAvailableSemaphore(size_t frameIndex) const;
		VkSemaphore GetRenderFinishedSemaphore(size_t frameIndex) const;
		VkFence GetInFlightFence(size_t frameIndex) const;

		void WaitForFence(size_t frameIndex) const;
		void ResetFence(size_t frameIndex) const;

		void Cleanup();

	private:

		void Init();

		VkDevice device;
		const size_t maxFramesInFlight;

		std::vector<VkSemaphore> imageAvailableSemaphores;
		std::vector<VkSemaphore> renderFinishedSemaphores;
		std::vector<VkFence> inFlightFences;

	};

}