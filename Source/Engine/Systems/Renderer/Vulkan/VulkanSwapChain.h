#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cstdint>

namespace Engine
{

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	class VulkanSwapChain
	{

	public:

		VulkanSwapChain
		(
			VkPhysicalDevice physicalDevice,
			VkDevice device,
			VkSurfaceKHR surface,
			uint32_t windowWidth,
			uint32_t windowHeight
		);

		~VulkanSwapChain();

		void Create(VkRenderPass renderPass);
		void Cleanup();
		void Recreate(uint32_t newWidth, uint32_t newHeight, VkRenderPass renderPass);

		// Accessors
		VkSwapchainKHR GetSwapchain() const { return swapChain; }
		VkFormat GetImageFormat() const { return swapChainImageFormat; }
		VkFormat GetDepthFormat() const { return depthFormat; }
		VkExtent2D GetExtent() const { return swapChainExtent; }
		VkFormat GetPendingImageFormat() const { return pendingImageFormat; }
		VkFormat GetPendingDepthFormat() const { return pendingDepthFormat; }
		const std::vector<VkFramebuffer>& GetFramebuffers() const { return swapChainFramebuffers; }

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

		VkResult AcquireNextImage(VkSemaphore imageAvailable, uint32_t* imageIndex);
		VkResult Present(VkQueue presentQueue, VkSemaphore* waitSemaphores, uint32_t imageIndex);

	private:

		// Core handles
		VkPhysicalDevice physicalDevice;
		VkDevice device;
		VkSurfaceKHR surface;
		uint32_t windowWidth;
		uint32_t windowHeight;

		// Swapchain outputs
		VkSwapchainKHR swapChain = VK_NULL_HANDLE;
		std::vector<VkImage> swapChainImages;
		std::vector<VkImageView> swapChainImageViews;
		VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;
		VkFormat depthFormat = VK_FORMAT_UNDEFINED;
		VkFormat pendingImageFormat = VK_FORMAT_UNDEFINED;
		VkFormat pendingDepthFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D swapChainExtent = {};

		// Depth buffers
		std::vector<VkImage> depthImages;
		std::vector<VkDeviceMemory> depthImageMemories;
		std::vector<VkImageView> depthImageViews;

		// Framebuffers
		std::vector<VkFramebuffer> swapChainFramebuffers;

		// Last render pass used
		VkRenderPass renderPassRef = VK_NULL_HANDLE;

		// Core creation steps
		void InitFormats(); 
		void CreateSwapChain();
		void CreateImageViews();
		void CreateDepthResources();
		void CreateFramebuffers();

		// Helpers
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
		VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
		VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
		VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
		VkFormat FindDepthFormat() const;

	};

}
