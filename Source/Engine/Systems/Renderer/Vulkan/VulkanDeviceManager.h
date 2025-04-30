#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <set>
#include <string>
#include <optional>
#include <windows.h>

namespace Engine
{

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool isComplete() const
		{
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	class VulkanDeviceManager
	{

	public:

		VulkanDeviceManager
		(
			HWND hwnd,
			uint32_t windowWidth,
			uint32_t windowHeight
		);

		~VulkanDeviceManager();

		// Creates a logical device from the chosen GPU and retrieves queue handles
		void CreateLogicalDevice();

		// Cleanup for logical device
		void Cleanup();

		// Used when creating the command pool 
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;

		// Getters
		VkDevice GetDevice() const { return device; }
		VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
		VkQueue GetGraphicsQueue() const { return graphicsQueue; }
		VkQueue GetPresentQueue() const { return presentQueue; }
		const QueueFamilyIndices& GetQueueFamilyIndices() const { return queueIndices; }
		const VkSurfaceKHR& GetSurface() const { return surface; }

		VkInstance GetInstance() const { return instance; }

	private:

		// Device selection and init
		void CreateInstance();
		void CreateSurface();
		void PickPhysicalDevice();

		bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
		bool IsDeviceSuitable(VkPhysicalDevice device) const;
		int RateDeviceSuitability(VkPhysicalDevice device) const;

	private:

		HWND windowHandle;
		uint32_t windowWidth = 0;
		uint32_t windowHeight = 0;

		VkInstance instance = VK_NULL_HANDLE;
		VkSurfaceKHR surface = VK_NULL_HANDLE;

		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;

		VkQueue graphicsQueue = VK_NULL_HANDLE;
		VkQueue presentQueue = VK_NULL_HANDLE;

		std::vector<const char*> deviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		// I feel like this should probably use vulkan defines
		std::vector<const char*> validationLayers = {
			"VK_LAYER_KHRONOS_validation",
		};

		bool enableValidationLayers = false;

		QueueFamilyIndices queueIndices;

	};

}
