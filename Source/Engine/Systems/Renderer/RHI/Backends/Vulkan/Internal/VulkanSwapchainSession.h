#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <vector>

namespace Swim::RhiVulkan
{

	// Acquisition/presentation state for one native swapchain generation. Native
	// images and views are owned by VulkanSwapchain; this class never destroys them.
	class VulkanSwapchainSession
	{
	public:
		explicit VulkanSwapchainSession(std::shared_ptr<VulkanDeviceState> state)
			: state(std::move(state))
		{
		}

		void SetImages(VkSwapchainKHR handle, std::uint32_t imageCount);
		bool IsSuspended() const
		{
			return suspended;
		}
		void Suspend();
		void Invalidate();
		void RequireNoAcquiredImages() const;
		Rhi::SwapchainAcquireResult Acquire(Rhi::Semaphore& signal, Rhi::Fence* fence);
		bool Present(Rhi::Queue& queue, std::uint32_t imageIndex, std::span<Rhi::Semaphore* const> waits);

	private:
		std::shared_ptr<VulkanDeviceState> state;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<bool> acquired;
		bool suspended = false;
		bool needsResize = true;
	};

} // namespace Swim::RhiVulkan
