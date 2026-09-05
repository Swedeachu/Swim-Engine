#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanSwapchain.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

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
		Rhi::Semaphore& signalSemaphore, Rhi::Fence* signalFence)
	{
		const auto size = window.GetPixelSize();
		if (window.IsMinimized() || size.Width == 0 || size.Height == 0)
		{
			session.Suspend();
		}
		return session.Acquire(signalSemaphore, signalFence);
	}

	bool VulkanSwapchain::Present(Rhi::Queue& queue, std::uint32_t imageIndex,
		std::span<Rhi::Semaphore* const> waits)
	{
		return session.Present(queue, imageIndex, waits);
	}

	bool VulkanSwapchain::Resize(Rhi::Extent2D requestedExtent, const Rhi::TimelinePoint& safeAfter)
	{
		if (!Rebuild(requestedExtent.Width, requestedExtent.Height, &safeAfter))
		{
			throw std::runtime_error("Failed to resize Vulkan swapchain; retry Resize before acquiring");
		}
		return !session.IsSuspended();
	}

	bool VulkanSwapchain::Rebuild(std::uint32_t width, std::uint32_t height, const Rhi::TimelinePoint* safeAfter)
	{
		if (desc.Hdr)
		{
			return false;
		}
		const auto pixelSize = window.GetPixelSize();
		if (width == 0 || height == 0 || window.IsMinimized() || pixelSize.Width == 0 || pixelSize.Height == 0)
		{
			session.Suspend();
			return true;
		}
		// The native surface can become zero-sized before SDL delivers its event.
		VkSurfaceCapabilitiesKHR capabilities{};
		if (state->Instance->Dispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
			state->Device.physical_device.physical_device, surface, &capabilities) != VK_SUCCESS)
		{
			session.Invalidate();
			return false;
		}
		if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 ||
			capabilities.maxImageExtent.width == 0 || capabilities.maxImageExtent.height == 0)
		{
			session.Suspend();
			return true;
		}
		session.RequireNoAcquiredImages();
		const bool replaceExisting = swapchain.swapchain != VK_NULL_HANDLE;
		if (replaceExisting && !WaitForRetirement(safeAfter))
		{
			return false;
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

		session.Invalidate();
		auto swapchainResult = builder.build();
		// Passing oldSwapchain retires it even if native creation fails. It must
		// never be acquired from, or passed as oldSwapchain on a later retry.
		// Retirement was completed before build, so release it on both paths.
		DestroySwapchain();
		if (!swapchainResult)
		{
			return false;
		}

		swapchain = std::move(swapchainResult).value();
		SetVulkanObjectName(*state, VK_OBJECT_TYPE_SWAPCHAIN_KHR, ToNativeHandle(swapchain.swapchain), "Swim swapchain");
		try
		{
			auto imagesResult = swapchain.get_images();
			if (!imagesResult)
			{
				DestroySwapchain();
				return false;
			}
			auto viewsResult = swapchain.get_image_views();
			if (!viewsResult)
			{
				DestroySwapchain();
				return false;
			}
			auto newImages = std::move(imagesResult).value();
			imageViews = std::move(viewsResult).value();
			if (newImages.empty() || newImages.size() != imageViews.size())
			{
				DestroySwapchain();
				return false;
			}
			format = FromVkFormat(swapchain.image_format);
			extent = { swapchain.extent.width, swapchain.extent.height };
			textures.reserve(newImages.size());
			views.reserve(newImages.size());
			for (std::size_t index = 0; index < newImages.size(); ++index)
			{
				Rhi::TextureDesc textureDesc{};
				textureDesc.DebugName = "Swapchain color";
				textureDesc.Dimension = Rhi::TextureDimension::Texture2D;
				textureDesc.Extent = { extent.Width, extent.Height, 1 };
				textureDesc.PixelFormat = format;
				textureDesc.Usage = Rhi::TextureUsage::ColorAttachment;
				textures.push_back(std::make_unique<VulkanTexture>(state, newImages[index], textureDesc));

				Rhi::TextureViewDesc viewDesc{};
				viewDesc.DebugName = "Swapchain color view";
				viewDesc.Dimension = Rhi::TextureViewDimension::Texture2D;
				viewDesc.PixelFormat = format;
				views.push_back(std::make_unique<VulkanTextureView>(
					state, *textures.back(), imageViews[index], viewDesc));
			}
			session.SetImages(swapchain.swapchain, static_cast<std::uint32_t>(views.size()));
			return true;
		}
		catch (...)
		{
			DestroySwapchain();
			throw;
		}
	}

	void VulkanSwapchain::DestroySwapchain()
	{
		session.Invalidate();
		extent = {};
		format = Rhi::Format::Undefined;
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
