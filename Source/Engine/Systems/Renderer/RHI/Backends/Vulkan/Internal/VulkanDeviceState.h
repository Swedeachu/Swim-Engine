#pragma once

// Shared Vulkan instance/device bootstrap state. Every backend object type
// (queues, resources, sync primitives, swapchain) holds a shared_ptr to a
// VulkanDeviceState so it can reach the loaded dispatch table and the VMA
// allocator without depending on the concrete VulkanDevice type.

#include "Engine/Platform/Internal/VulkanWsi.h"

#include <volk.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Swim::RhiVulkan
{

	struct QueueFamilySelection
	{
		std::uint32_t Graphics = UINT32_MAX;
		std::uint32_t Compute = UINT32_MAX;
		std::uint32_t Transfer = UINT32_MAX;

		bool IsValid() const
		{
			return Graphics != UINT32_MAX && Compute != UINT32_MAX && Transfer != UINT32_MAX;
		}
	};

	struct VulkanInstanceState
	{
		vkb::Instance Instance{};
		volk::VolkInstanceTable Dispatch{};
		bool LoaderAcquired = false;

		~VulkanInstanceState()
		{
			if (Instance.instance != VK_NULL_HANDLE)
			{
				vkb::destroy_instance(Instance);
			}
			if (LoaderAcquired)
			{
				Platform::Internal::ReleaseVulkanLoader();
			}
		}
	};

	struct VulkanDeviceState
	{
		std::shared_ptr<VulkanInstanceState> Instance;
		vkb::Device Device{};
		volk::VolkDeviceTable Dispatch{};
		QueueFamilySelection QueueFamilies{};
		VkQueue PresentationQueue = VK_NULL_HANDLE;
		std::shared_ptr<std::mutex> PresentationQueueMutex;
		VmaVulkanFunctions AllocatorFunctions{};
		VmaAllocator Allocator = nullptr;
		std::atomic<std::uint32_t> SamplerCount{ 0 };

		~VulkanDeviceState()
		{
			if (Allocator != nullptr)
			{
				vmaDestroyAllocator(Allocator);
			}
			if (Device.device != VK_NULL_HANDLE)
			{
				vkb::destroy_device(Device);
			}
		}
	};

} // namespace Swim::RhiVulkan
