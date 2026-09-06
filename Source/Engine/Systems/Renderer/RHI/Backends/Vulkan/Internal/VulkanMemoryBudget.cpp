#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanMemoryBudget.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <array>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	Rhi::MemoryBudgetSnapshot BuildVulkanMemoryBudgetSnapshot(const VkPhysicalDeviceMemoryProperties& properties,
		std::span<const VmaBudget> allocatorBudgets, const VkPhysicalDeviceMemoryBudgetPropertiesEXT* driverBudget)
	{
		if (properties.memoryHeapCount == 0 || properties.memoryHeapCount > VK_MAX_MEMORY_HEAPS ||
			properties.memoryTypeCount > VK_MAX_MEMORY_TYPES || allocatorBudgets.size() < properties.memoryHeapCount)
		{
			throw std::runtime_error("Invalid Vulkan memory telemetry heap/type counts");
		}
		Rhi::MemoryBudgetSnapshot snapshot;
		snapshot.Heaps.reserve(properties.memoryHeapCount);
		for (std::uint32_t index = 0; index < properties.memoryHeapCount; ++index)
		{
			const auto& nativeHeap = properties.memoryHeaps[index];
			const auto& statistics = allocatorBudgets[index].statistics;
			Rhi::MemoryHeapBudget heap{};
			heap.HeapIndex = index;
			heap.CapacityBytes = nativeHeap.size;
			heap.DeviceLocal = (nativeHeap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
			heap.AllocationCount = statistics.allocationCount;
			heap.AllocationBytes = statistics.allocationBytes;
			heap.BlockCount = statistics.blockCount;
			heap.BlockBytes = statistics.blockBytes;
			// Match VMA's 80% fallback heuristic, without overflowing size * 8.
			// Ignore VMA's cached usage/budget: it may include a driver estimate
			// or an internal fallback whose provenance cannot be distinguished.
			heap.UsageBytes = heap.BlockBytes;
			heap.BudgetBytes = (heap.CapacityBytes / 10) * 8 + (heap.CapacityBytes % 10) * 8 / 10;
			if (driverBudget != nullptr && driverBudget->heapBudget[index] != 0 &&
				driverBudget->heapBudget[index] <= heap.CapacityBytes)
			{
				heap.Source = Rhi::MemoryBudgetSource::DriverEstimate;
				heap.UsageBytes = driverBudget->heapUsage[index];
				heap.BudgetBytes = driverBudget->heapBudget[index];
			}
			snapshot.Heaps.push_back(heap);
		}
		for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
		{
			const auto& type = properties.memoryTypes[index];
			if (type.heapIndex >= snapshot.Heaps.size())
			{
				throw std::runtime_error("Invalid Vulkan memory telemetry heap index");
			}
			if ((type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
			{
				snapshot.Heaps[type.heapIndex].HostVisible = true;
			}
		}
		return snapshot;
	}

	Rhi::MemoryBudgetSnapshot QueryVulkanMemoryBudget(const VulkanDeviceState& state)
	{
		RequireVulkanDevice(state);
		if (state.Allocator == nullptr)
		{
			return {};
		}
		const VkPhysicalDeviceMemoryProperties* properties = nullptr;
		vmaGetMemoryProperties(state.Allocator, &properties);
		std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> allocatorBudgets{};
		vmaGetHeapBudgets(state.Allocator, allocatorBudgets.data());
		RequireVulkanDevice(state);

		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
		budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
		const bool queryDriver = state.MemoryBudgetEnabled && state.Instance &&
			state.Instance->Dispatch.vkGetPhysicalDeviceMemoryProperties2 != nullptr;
		if (queryDriver)
		{
			VkPhysicalDeviceMemoryProperties2 memory{};
			memory.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			memory.pNext = &budget;
			// Query current driver estimates explicitly. VMA caches its budgets
			// between allocation/frame updates, including while this app is idle.
			state.Instance->Dispatch.vkGetPhysicalDeviceMemoryProperties2(
				state.Device.physical_device.physical_device, &memory);
		}
		// A concurrent native failure must not turn into apparently healthy data.
		RequireVulkanDevice(state);
		return BuildVulkanMemoryBudgetSnapshot(*properties, allocatorBudgets, queryDriver ? &budget : nullptr);
	}

} // namespace Swim::RhiVulkan
