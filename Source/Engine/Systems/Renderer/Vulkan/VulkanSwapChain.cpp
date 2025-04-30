#include "PCH.h"
#include "VulkanSwapChain.h"
#include <stdexcept>
#include <algorithm>

namespace Engine
{

	VulkanSwapChain::VulkanSwapChain
	(
		VkPhysicalDevice physicalDevice,
		VkDevice device,
		VkSurfaceKHR surface,
		uint32_t windowWidth,
		uint32_t windowHeight
	)
		: physicalDevice(physicalDevice),
		device(device),
		surface(surface),
		windowWidth(windowWidth),
		windowHeight(windowHeight)
	{
		// we let VulkanRenderer call Create(rp) for us once it makes the render pass
		// but we do call InitFormats here
		InitFormats();
	}

	// kinda scuffed we just save these in 'pending' fields for the renderpass to use on creation, it ultimately doesn't matter much
	void VulkanSwapChain::InitFormats()
	{
		SwapChainSupportDetails support = QuerySwapChainSupport(physicalDevice);
		VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
		pendingImageFormat = surfaceFormat.format;
		pendingDepthFormat = FindDepthFormat();
	}

	VulkanSwapChain::~VulkanSwapChain()
	{
		Cleanup();
	}

	void VulkanSwapChain::Create(VkRenderPass renderPass)
	{
		renderPassRef = renderPass;
		CreateSwapChain();
		CreateImageViews();
		CreateDepthResources();
		CreateFramebuffers();
	}

	void VulkanSwapChain::Recreate(uint32_t newWidth, uint32_t newHeight, VkRenderPass renderPass)
	{
		vkDeviceWaitIdle(device);

		windowWidth = newWidth;
		windowHeight = newHeight;
		renderPassRef = renderPass;
		Cleanup();
		Create(renderPass);
	}

	void VulkanSwapChain::Cleanup()
	{
		for (auto fb : swapChainFramebuffers)
		{
			vkDestroyFramebuffer(device, fb, nullptr);
		}
		swapChainFramebuffers.clear();

		for (auto view : depthImageViews)
		{
			vkDestroyImageView(device, view, nullptr);
		}
		for (auto image : depthImages)
		{
			vkDestroyImage(device, image, nullptr);
		}
		for (auto mem : depthImageMemories)
		{
			vkFreeMemory(device, mem, nullptr);
		}
		depthImages.clear();
		depthImageMemories.clear();
		depthImageViews.clear();

		for (auto view : swapChainImageViews)
		{
			vkDestroyImageView(device, view, nullptr);
		}
		swapChainImageViews.clear();

		if (swapChain)
		{
			vkDestroySwapchainKHR(device, swapChain, nullptr);
			swapChain = VK_NULL_HANDLE;
		}
	}

	void VulkanSwapChain::CreateSwapChain()
	{
		SwapChainSupportDetails support = QuerySwapChainSupport(physicalDevice);

		VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
		VkPresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
		VkExtent2D extent = ChooseExtent(support.capabilities);

		uint32_t imageCount = support.capabilities.minImageCount + 1;
		if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
		{
			imageCount = support.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = support.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;

		if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create swapchain!");
		}

		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
		swapChainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;
	}

	void VulkanSwapChain::CreateImageViews()
	{
		swapChainImageViews.resize(swapChainImages.size());

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = swapChainImages[i];
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = swapChainImageFormat;
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create swapchain image views!");
			}
		}
	}

	void VulkanSwapChain::CreateDepthResources()
	{
		depthFormat = FindDepthFormat();

		depthImages.resize(swapChainImages.size());
		depthImageMemories.resize(swapChainImages.size());
		depthImageViews.resize(swapChainImages.size());

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent.width = swapChainExtent.width;
			imageInfo.extent.height = swapChainExtent.height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.format = depthFormat;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateImage(device, &imageInfo, nullptr, &depthImages[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create depth image!");
			}

			VkMemoryRequirements memReq;
			vkGetImageMemoryRequirements(device, depthImages[i], &memReq);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			if (vkAllocateMemory(device, &allocInfo, nullptr, &depthImageMemories[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to allocate depth memory!");
			}

			vkBindImageMemory(device, depthImages[i], depthImageMemories[i], 0);

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = depthImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = depthFormat;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageViews[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create depth image view!");
			}
		}
	}

	void VulkanSwapChain::CreateFramebuffers()
	{
		swapChainFramebuffers.resize(swapChainImageViews.size());

		for (size_t i = 0; i < swapChainImageViews.size(); i++)
		{
			std::array<VkImageView, 2> attachments = {
				swapChainImageViews[i],
				depthImageViews[i]
			};

			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = renderPassRef;
			fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			fbInfo.pAttachments = attachments.data();
			fbInfo.width = swapChainExtent.width;
			fbInfo.height = swapChainExtent.height;
			fbInfo.layers = 1;

			if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create framebuffer!");
			}
		}
	}

	SwapChainSupportDetails VulkanSwapChain::QuerySwapChainSupport(VkPhysicalDevice dev) const
	{
		SwapChainSupportDetails details;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &details.capabilities);

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, nullptr);
		if (formatCount > 0)
		{
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, details.formats.data());
		}

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, nullptr);
		if (presentModeCount > 0)
		{
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}

	VkSurfaceFormatKHR VulkanSwapChain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
	{
		for (const auto& format : formats)
		{
			if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
			{
				return format;
			}
		}
		return formats[0];
	}

	VkPresentModeKHR VulkanSwapChain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
	{
		for (const auto& mode : modes)
		{
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return mode;
			}
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D VulkanSwapChain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
	{
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			return capabilities.currentExtent;
		}

		VkExtent2D extent = { windowWidth, windowHeight };
		extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		return extent;
	}

	VkFormat VulkanSwapChain::FindDepthFormat() const
	{
		std::vector<VkFormat> formats = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D32_SFLOAT_S8_UINT
		};

		for (VkFormat fmt : formats)
		{
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);

			if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			{
				return fmt;
			}
		}
		throw std::runtime_error("Failed to find supported depth format!");
	}

	uint32_t VulkanSwapChain::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1 << i)) &&
				(memProps.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type!");
	}

	VkResult VulkanSwapChain::AcquireNextImage(VkSemaphore imageAvailable, uint32_t* imageIndex)
	{
		return vkAcquireNextImageKHR(
			device,
			swapChain,
			UINT64_MAX,
			imageAvailable,
			VK_NULL_HANDLE,
			imageIndex
		);
	}

	VkResult VulkanSwapChain::Present(VkQueue presentQueue, VkSemaphore* waitSemaphores, uint32_t imageIndex)
	{
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = waitSemaphores;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain;
		presentInfo.pImageIndices = &imageIndex;

		return vkQueuePresentKHR(presentQueue, &presentInfo);
	}

}
