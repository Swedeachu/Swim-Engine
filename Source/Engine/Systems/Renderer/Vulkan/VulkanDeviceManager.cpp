#include "PCH.h"
#include "VulkanDeviceManager.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace Engine
{

	VulkanDeviceManager::VulkanDeviceManager
	(
		HWND hwnd,
		uint32_t width,
		uint32_t height
	)
		: windowHandle(hwnd),
		windowWidth(width),
		windowHeight(height)
	{
		enableValidationLayers = !validationLayers.empty();

		CreateInstance();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
	}

	VulkanDeviceManager::~VulkanDeviceManager()
	{
		// Destroy logical device if it exists
		if (device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}

		// Destroy surface
		if (surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(instance, surface, nullptr);
			surface = VK_NULL_HANDLE;
		}

		// Destroy Vulkan instance
		if (instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
	}

	void VulkanDeviceManager::CreateInstance()
	{
		if (enableValidationLayers)
		{
			uint32_t layerCount;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
			std::vector<VkLayerProperties> availableLayers(layerCount);
			vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

			for (const char* layerName : validationLayers)
			{
				bool found = false;
				for (const auto& prop : availableLayers)
				{
					if (strcmp(prop.layerName, layerName) == 0)
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					throw std::runtime_error("Requested validation layer not found.");
				}
			}
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "SwimEngine";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "SwimEngine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_2;

		std::vector<const char*> extensions = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			"VK_KHR_win32_surface" // couldn't find a define for this one in vulkan_core.h
		};

		if (enableValidationLayers)
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

		if (enableValidationLayers)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create Vulkan instance.");
		}
	}

	void VulkanDeviceManager::CreateSurface()
	{
		VkWin32SurfaceCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hwnd = windowHandle;
		createInfo.hinstance = GetModuleHandle(nullptr);

		if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create Win32 surface!");
		}
	}

	void VulkanDeviceManager::PickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		if (deviceCount == 0)
		{
			throw std::runtime_error("No Vulkan-compatible GPUs found!");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		struct DeviceScore
		{
			VkPhysicalDevice device;
			int score;
		};

		std::vector<DeviceScore> scoredDevices;

		for (const auto& dev : devices)
		{
			if (IsDeviceSuitable(dev))
			{
				scoredDevices.push_back({ dev, RateDeviceSuitability(dev) });
			}
		}

		if (scoredDevices.empty())
		{
			throw std::runtime_error("No suitable GPU found!");
		}

		std::sort(scoredDevices.begin(), scoredDevices.end(),
			[](const DeviceScore& a, const DeviceScore& b)
		{
			return a.score > b.score;
		});

		physicalDevice = scoredDevices.front().device;
		queueIndices = FindQueueFamilies(physicalDevice);

		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physicalDevice, &props);

		std::cout << "Using GPU: " << props.deviceName << std::endl;
	}

	void VulkanDeviceManager::CreateLogicalDevice()
	{
		std::set<uint32_t> uniqueQueues = {
			queueIndices.graphicsFamily.value(),
			queueIndices.presentFamily.value()
		};

		float priority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queueInfos;

		for (uint32_t queueFamily : uniqueQueues)
		{
			VkDeviceQueueCreateInfo queueInfo{};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = queueFamily;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &priority;
			queueInfos.push_back(queueInfo);
		}

		// --- Enable standard device features ---
		VkPhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.samplerAnisotropy = VK_TRUE;

		// --- Enable descriptor indexing features for bindless ---
		VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
		indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		indexingFeatures.runtimeDescriptorArray = VK_TRUE;
		indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
		indexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;

		// --- Use VkPhysicalDeviceFeatures2 chain to pass both ---
		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.features = deviceFeatures;
		features2.pNext = &indexingFeatures;

		// Add VK_EXT_descriptor_indexing to device extensions if not already present
		if (std::find(deviceExtensions.begin(), deviceExtensions.end(), VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == deviceExtensions.end())
		{
			deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
		}

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
		createInfo.pQueueCreateInfos = queueInfos.data();
		createInfo.pNext = &features2;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		if (enableValidationLayers)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
			createInfo.ppEnabledLayerNames = nullptr;
		}

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create logical device!");
		}

		vkGetDeviceQueue(device, queueIndices.graphicsFamily.value(), 0, &graphicsQueue);
		vkGetDeviceQueue(device, queueIndices.presentFamily.value(), 0, &presentQueue);
	}

	void VulkanDeviceManager::Cleanup()
	{
		if (device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}
	}

	bool VulkanDeviceManager::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
	{
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> required(deviceExtensions.begin(), deviceExtensions.end());

		for (const auto& ext : availableExtensions)
		{
			required.erase(ext.extensionName);
		}

		return required.empty();
	}

	bool VulkanDeviceManager::IsDeviceSuitable(VkPhysicalDevice device) const
	{
		QueueFamilyIndices indices = FindQueueFamilies(device);

		bool extensionsSupported = CheckDeviceExtensionSupport(device);

		bool swapChainAdequate = false;
		if (extensionsSupported)
		{
			uint32_t formatCount = 0;
			uint32_t presentModeCount = 0;
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
			swapChainAdequate = (formatCount > 0) && (presentModeCount > 0);
		}

		return indices.isComplete() && extensionsSupported && swapChainAdequate;
	}

	int VulkanDeviceManager::RateDeviceSuitability(VkPhysicalDevice device) const
	{
		VkPhysicalDeviceProperties props;
		VkPhysicalDeviceFeatures features;

		vkGetPhysicalDeviceProperties(device, &props);
		vkGetPhysicalDeviceFeatures(device, &features);

		if (!features.geometryShader)
		{
			return 0;
		}

		int score = 0;

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			score += 1000;
		}
		else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
		{
			score += 500;
		}

		score += props.limits.maxImageDimension2D;

		return score;
	}

	QueueFamilyIndices VulkanDeviceManager::FindQueueFamilies(VkPhysicalDevice device) const
	{
		QueueFamilyIndices indices;

		uint32_t queueCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);

		std::vector<VkQueueFamilyProperties> queues(queueCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

		for (uint32_t i = 0; i < queues.size(); i++)
		{
			if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				indices.graphicsFamily = i;
			}

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
			if (presentSupport)
			{
				indices.presentFamily = i;
			}

			if (indices.isComplete())
			{
				break;
			}
		}

		return indices;
	}

}
