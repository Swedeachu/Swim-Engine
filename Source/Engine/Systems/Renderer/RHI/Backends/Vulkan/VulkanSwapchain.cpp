#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanSwapchain.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanFence.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanSemaphore.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanTimeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	VulkanSwapchain::~VulkanSwapchain()
	{
		if (swapchain.swapchain != VK_NULL_HANDLE && state->PresentationQueue != VK_NULL_HANDLE)
		{
			std::scoped_lock lock(*state->PresentationQueueMutex);
			state->Dispatch.vkQueueWaitIdle(state->PresentationQueue);
		}
		DestroySwapchain();
		if (surface != VK_NULL_HANDLE)
		{
			Platform::Internal::DestroyVulkanSurface(
				ToNativeHandle(state->Instance->Instance.instance), ToNativeHandle(surface));
			surface = VK_NULL_HANDLE;
		}
	}

	bool VulkanSwapchain::Initialize()
	{
		const Platform::Extent2D pixelSize = window.GetPixelSize();
		return Rebuild(pixelSize.Width, pixelSize.Height, nullptr);
	}

	Rhi::SwapchainAcquireResult VulkanSwapchain::AcquireNextImage(
		Rhi::Semaphore& signalSemaphore,
		Rhi::Fence* signalFence)
	{
		auto* semaphore = dynamic_cast<VulkanSemaphore*>(&signalSemaphore);
		auto* fence = signalFence ? dynamic_cast<VulkanFence*>(signalFence) : nullptr;
		if (semaphore == nullptr || semaphore->GetState().get() != state.get() ||
			(signalFence != nullptr && (fence == nullptr || fence->GetState().get() != state.get())))
		{
			throw std::invalid_argument("Vulkan swapchain acquire requires same-device Vulkan synchronization objects");
		}

		Rhi::SwapchainAcquireResult result{};
		const VkResult vkResult = state->Dispatch.vkAcquireNextImageKHR(
			state->Device.device,
			swapchain.swapchain,
			UINT64_MAX,
			semaphore->GetSemaphore(),
			fence ? fence->GetFence() : VK_NULL_HANDLE,
			&result.ImageIndex);

		result.OutOfDate = vkResult == VK_ERROR_OUT_OF_DATE_KHR;
		result.Suboptimal = vkResult == VK_SUBOPTIMAL_KHR;
		if (vkResult != VK_SUCCESS && !result.OutOfDate && !result.Suboptimal)
		{
			throw std::runtime_error("Failed to acquire Vulkan swapchain image");
		}
		return result;
	}

	bool VulkanSwapchain::Present(
		Rhi::Queue& queue,
		std::uint32_t imageIndex,
		std::span<Rhi::Semaphore* const> waits)
	{
		auto* vulkanQueue = dynamic_cast<VulkanQueue*>(&queue);
		if (!vulkanQueue || vulkanQueue->GetFamilyIndex() != state->QueueFamilies.Graphics)
		{
			throw std::invalid_argument("Vulkan swapchain presentation requires the device graphics/present queue");
		}

		std::vector<VkSemaphore> waitSemaphores;
		waitSemaphores.reserve(waits.size());
		for (Rhi::Semaphore* wait : waits)
		{
			auto* semaphore = dynamic_cast<VulkanSemaphore*>(wait);
			if (semaphore == nullptr || semaphore->GetState().get() != state.get())
			{
				throw std::invalid_argument("Vulkan swapchain presentation requires same-device Vulkan semaphores");
			}
			waitSemaphores.push_back(semaphore->GetSemaphore());
		}

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = static_cast<std::uint32_t>(waitSemaphores.size());
		presentInfo.pWaitSemaphores = waitSemaphores.data();
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain.swapchain;
		presentInfo.pImageIndices = &imageIndex;

		std::scoped_lock lock(vulkanQueue->GetMutex());
		const VkResult result = state->Dispatch.vkQueuePresentKHR(vulkanQueue->GetQueue(), &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			return false;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("Failed to present Vulkan swapchain image");
		}
		return true;
	}

	void VulkanSwapchain::Resize(Rhi::Extent2D requestedExtent, const Rhi::TimelinePoint& safeAfter)
	{
		if (requestedExtent.Width == 0 || requestedExtent.Height == 0)
		{
			return;
		}
		if (!Rebuild(requestedExtent.Width, requestedExtent.Height, &safeAfter))
		{
			throw std::runtime_error("Failed to resize Vulkan swapchain");
		}
	}

	bool VulkanSwapchain::Rebuild(std::uint32_t width, std::uint32_t height, const Rhi::TimelinePoint* safeAfter)
	{
		if (width == 0 || height == 0 || desc.Hdr)
		{
			return false;
		}

		const bool replaceExisting = swapchain.swapchain != VK_NULL_HANDLE;
		std::shared_ptr<VulkanTimelineState> retirementTimeline;
		std::uint64_t retirementValue = 0;
		if (replaceExisting)
		{
			if (safeAfter == nullptr)
			{
				throw std::invalid_argument("Vulkan swapchain replacement requires a GPU timeline retirement point");
			}
			auto* timeline = dynamic_cast<VulkanTimeline*>(safeAfter->Semaphore);
			if (timeline == nullptr || timeline->GetState()->DeviceState.get() != state.get())
			{
				throw std::invalid_argument("Vulkan swapchain retirement timeline must belong to the same device");
			}
			retirementTimeline = timeline->GetState();
			retirementValue = safeAfter->Value;
		}

		vkb::SwapchainBuilder builder{ state->Device, surface };
		if (replaceExisting)
		{
			builder.set_old_swapchain(swapchain);
		}

		const VkFormat preferredFormat = ToVkFormat(desc.PreferredFormat);
		if (preferredFormat != VK_FORMAT_UNDEFINED)
		{
			builder.set_desired_format({ preferredFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
		}
		builder
			.add_fallback_format({ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
			.set_desired_extent(width, height)
			.set_desired_min_image_count(std::max(2u, desc.ImageCount))
			.set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

		if (desc.Vsync)
		{
			builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
		}
		else
		{
			builder
				.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
				.add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
				.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
		}

		auto swapchainResult = builder.build();
		if (!swapchainResult)
		{
			return false;
		}

		vkb::Swapchain newSwapchain = std::move(swapchainResult).value();
		auto imagesResult = newSwapchain.get_images();
		auto viewsResult = newSwapchain.get_image_views();
		if (!imagesResult || !viewsResult)
		{
			vkb::destroy_swapchain(newSwapchain);
			return false;
		}

		auto newImages = std::move(imagesResult).value();
		auto newViews = std::move(viewsResult).value();
		if (newImages.size() != newViews.size())
		{
			newSwapchain.destroy_image_views(newViews);
			vkb::destroy_swapchain(newSwapchain);
			return false;
		}

		if (replaceExisting)
		{
			std::uint64_t completedValue = 0;
			if (retirementTimeline->DeviceState->Dispatch.vkGetSemaphoreCounterValue(
				retirementTimeline->DeviceState->Device.device,
				retirementTimeline->Semaphore,
				&completedValue) != VK_SUCCESS)
			{
				newSwapchain.destroy_image_views(newViews);
				vkb::destroy_swapchain(newSwapchain);
				return false;
			}
			if (completedValue < retirementValue)
			{
				VkSemaphoreWaitInfo waitInfo{};
				waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
				waitInfo.semaphoreCount = 1;
				waitInfo.pSemaphores = &retirementTimeline->Semaphore;
				waitInfo.pValues = &retirementValue;
				if (retirementTimeline->DeviceState->Dispatch.vkWaitSemaphores(
					retirementTimeline->DeviceState->Device.device, &waitInfo, UINT64_MAX) != VK_SUCCESS)
				{
					newSwapchain.destroy_image_views(newViews);
					vkb::destroy_swapchain(newSwapchain);
					return false;
				}
			}

			// The frame timeline proves rendering is finished, but core Vulkan does not
			// expose presentation-engine completion on that timeline. Serialize only
			// the presentation queue here before retiring the old WSI objects; this
			// avoids the former device-wide idle while remaining correct on drivers
			// without swapchain-maintenance present fences.
			std::scoped_lock lock(*state->PresentationQueueMutex);
			if (state->Dispatch.vkQueueWaitIdle(state->PresentationQueue) != VK_SUCCESS)
			{
				newSwapchain.destroy_image_views(newViews);
				vkb::destroy_swapchain(newSwapchain);
				return false;
			}
		}

		views.clear();
		textures.clear();
		if (replaceExisting)
		{
			DestroySwapchain();
		}

		swapchain = std::move(newSwapchain);
		imageViews = std::move(newViews);
		format = FromVkFormat(swapchain.image_format);
		extent = { swapchain.extent.width, swapchain.extent.height };

		textures.reserve(newImages.size());
		views.reserve(newImages.size());
		for (std::size_t index = 0; index < newImages.size(); ++index)
		{
			Rhi::TextureDesc textureDesc{};
			textureDesc.Dimension = Rhi::TextureDimension::Texture2D;
			textureDesc.Extent = { extent.Width, extent.Height, 1 };
			textureDesc.PixelFormat = format;
			textureDesc.Usage = Rhi::TextureUsage::ColorAttachment;
			textures.push_back(std::make_unique<VulkanTexture>(state, newImages[index], textureDesc));

			Rhi::TextureViewDesc viewDesc{};
			viewDesc.Dimension = Rhi::TextureViewDimension::Texture2D;
			viewDesc.PixelFormat = format;
			views.push_back(std::make_unique<VulkanTextureView>(
				state, *textures.back(), imageViews[index], viewDesc));
		}
		return true;
	}

	void VulkanSwapchain::DestroySwapchain()
	{
		views.clear();
		textures.clear();
		if (!imageViews.empty() && swapchain.swapchain != VK_NULL_HANDLE)
		{
			swapchain.destroy_image_views(imageViews);
			imageViews.clear();
		}
		if (swapchain.swapchain != VK_NULL_HANDLE)
		{
			vkb::destroy_swapchain(swapchain);
			swapchain = {};
		}
	}

} // namespace Swim::RhiVulkan
