#pragma once

// Shared Vulkan instance/device bootstrap state. Every backend object type
// (queues, resources, sync primitives, swapchain) holds a shared_ptr to a
// VulkanDeviceState so it can reach the loaded dispatch table and the VMA
// allocator without depending on the concrete VulkanDevice type.

#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"

#include <volk.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceLoss.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

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
		VulkanDiagnosticsState Diagnostics;
		vkb::Instance Instance{};
		volk::VolkInstanceTable Dispatch{};
		bool LoaderAcquired = false;
		bool RequestDeviceFaultDiagnostics = true;

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
		std::shared_ptr<Rhi::DeviceDiagnostics> Diagnostics = std::make_shared<Rhi::DeviceDiagnostics>();
		bool DeviceFaultEnabled = false;
		std::array<std::shared_ptr<std::mutex>, 3> QueueMutexes{};
		mutable std::mutex RetirementMutex;
		mutable bool LossRetirementAttempted = false;
		vkb::Device Device{};
		volk::VolkDeviceTable Dispatch{};
		QueueFamilySelection QueueFamilies{};
		std::vector<VkQueueFamilyProperties> QueueProperties;
		VkQueue PresentationQueue = VK_NULL_HANDLE;
		std::shared_ptr<std::mutex> PresentationQueueMutex;
		VmaVulkanFunctions AllocatorFunctions{};
		VmaAllocator Allocator = nullptr;
		std::atomic<std::uint32_t> SamplerCount{ 0 };

		~VulkanDeviceState()
		{
			RetireLostVulkanDevice(*this);
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
