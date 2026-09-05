#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSwapchainSession.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanFence.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanSemaphore.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	void VulkanSwapchainSession::SetImages(VkSwapchainKHR handle, std::uint32_t imageCount)
	{
		RequireNoAcquiredImages();
		acquired.assign(imageCount, false);
		swapchain = handle;
		suspended = false;
		needsResize = handle == VK_NULL_HANDLE || imageCount == 0;
	}

	void VulkanSwapchainSession::Suspend()
	{
		suspended = true;
		needsResize = true;
	}

	void VulkanSwapchainSession::Invalidate()
	{
		needsResize = true;
	}

	void VulkanSwapchainSession::RequireNoAcquiredImages() const
	{
		if (std::find(acquired.begin(), acquired.end(), true) != acquired.end())
		{
			throw std::logic_error("Present all acquired swapchain images before replacement");
		}
	}

	Rhi::SwapchainAcquireResult VulkanSwapchainSession::Acquire(Rhi::Semaphore& signal, Rhi::Fence* signalFence)
	{
		auto* semaphore = dynamic_cast<VulkanSemaphore*>(&signal);
		auto* fence = signalFence ? dynamic_cast<VulkanFence*>(signalFence) : nullptr;
		if (semaphore == nullptr || semaphore->GetState().get() != state.get() ||
			(signalFence != nullptr && (fence == nullptr || fence->GetState().get() != state.get())))
		{
			throw std::invalid_argument("Vulkan swapchain acquire requires same-device Vulkan synchronization objects");
		}

		Rhi::SwapchainAcquireResult result{};
		if (suspended || needsResize)
		{
			result.Suspended = suspended;
			result.OutOfDate = !suspended;
			return result;
		}

		// Occlusion/minimization can prevent WSI forward progress. A finite wait
		// returns control to the event pump without requiring a signaled semaphore.
		std::uint32_t imageIndex = UINT32_MAX;
		constexpr std::uint64_t acquireTimeout = 16'000'000;
		const VkResult nativeResult = state->Dispatch.vkAcquireNextImageKHR(
			state->Device.device, swapchain, acquireTimeout, semaphore->GetSemaphore(),
			fence ? fence->GetFence() : VK_NULL_HANDLE, &imageIndex);
		if (nativeResult == VK_TIMEOUT || nativeResult == VK_NOT_READY)
		{
			result.NotReady = true;
			return result;
		}
		if (nativeResult == VK_ERROR_OUT_OF_DATE_KHR)
		{
			Invalidate();
			result.OutOfDate = true;
			return result;
		}
		if (nativeResult != VK_SUCCESS && nativeResult != VK_SUBOPTIMAL_KHR)
		{
			Invalidate();
			throw std::runtime_error("Failed to acquire Vulkan swapchain image: " + std::to_string(nativeResult));
		}
		if (imageIndex >= acquired.size() || acquired[imageIndex])
		{
			Invalidate();
			throw std::runtime_error("Vulkan returned an invalid or already acquired swapchain image");
		}
		acquired[imageIndex] = true;
		result.ImageIndex = imageIndex;
		result.Suboptimal = nativeResult == VK_SUBOPTIMAL_KHR;
		if (result.Suboptimal)
		{
			Invalidate();
		}
		return result;
	}

	bool VulkanSwapchainSession::Present(Rhi::Queue& queue, std::uint32_t imageIndex,
		std::span<Rhi::Semaphore* const> waits)
	{
		auto* vulkanQueue = dynamic_cast<VulkanQueue*>(&queue);
		if (vulkanQueue == nullptr || vulkanQueue->GetState().get() != state.get() ||
			vulkanQueue->GetFamilyIndex() != state->QueueFamilies.Graphics)
		{
			throw std::invalid_argument("Vulkan presentation requires the same-device graphics/present queue");
		}
		if (imageIndex >= acquired.size() || !acquired[imageIndex])
		{
			throw std::invalid_argument("Vulkan presentation requires a currently acquired image");
		}
		std::vector<VkSemaphore> waitSemaphores;
		waitSemaphores.reserve(waits.size());
		for (Rhi::Semaphore* wait : waits)
		{
			auto* semaphore = dynamic_cast<VulkanSemaphore*>(wait);
			if (semaphore == nullptr || semaphore->GetState().get() != state.get() ||
				std::find(waitSemaphores.begin(), waitSemaphores.end(), semaphore->GetSemaphore()) != waitSemaphores.end())
			{
				throw std::invalid_argument("Vulkan presentation requires unique same-device semaphores");
			}
			waitSemaphores.push_back(semaphore->GetSemaphore());
		}
		VkPresentInfoKHR info{};
		info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		info.waitSemaphoreCount = static_cast<std::uint32_t>(waitSemaphores.size());
		info.pWaitSemaphores = waitSemaphores.data();
		info.swapchainCount = 1;
		info.pSwapchains = &swapchain;
		info.pImageIndices = &imageIndex;

		std::scoped_lock lock(vulkanQueue->GetMutex());
		const VkResult result = state->Dispatch.vkQueuePresentKHR(vulkanQueue->GetQueue(), &info);
		acquired[imageIndex] = false;
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			Invalidate();
			return false;
		}
		if (result != VK_SUCCESS)
		{
			Invalidate();
			throw std::runtime_error("Failed to present Vulkan swapchain image: " + std::to_string(result));
		}
		return !needsResize;
	}

} // namespace Swim::RhiVulkan
