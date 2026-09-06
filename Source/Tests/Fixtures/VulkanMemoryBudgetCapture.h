#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"

#include <atomic>

namespace Swim::Testing
{

	// Real VMA allocator/counters over captured native allocation calls. No
	// Vulkan loader or GPU is involved; driver budget values are controlled.
	struct VulkanMemoryBudgetCapture
	{
		explicit VulkanMemoryBudgetCapture(bool driverBudget = true);
		~VulkanMemoryBudgetCapture();
		VmaAllocation Allocate(std::uint64_t bytes, std::uint32_t memoryType = 0, bool dedicated = true);
		void Free(VmaAllocation allocation);

		std::shared_ptr<RhiVulkan::VulkanDeviceState> State = std::make_shared<RhiVulkan::VulkanDeviceState>();
		std::unique_ptr<RhiVulkan::VulkanDevice> Device;
		VkPhysicalDeviceMemoryProperties Properties{};
		VkPhysicalDeviceMemoryBudgetPropertiesEXT Driver{};
		std::atomic<std::uint32_t> DriverCalls{ 0 };
		std::atomic<std::uint32_t> NativeAllocations{ 0 };
		std::atomic<std::uint32_t> NativeFrees{ 0 };
		std::atomic<std::uint32_t> IdleCalls{ 0 };
		bool LoseDuringQuery = false;

	private:
		std::vector<VmaAllocation> allocations;
	};

} // namespace Swim::Testing
