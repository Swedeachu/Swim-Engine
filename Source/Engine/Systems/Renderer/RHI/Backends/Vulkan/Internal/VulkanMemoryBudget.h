#pragma once

#include "Engine/Systems/Renderer/RHI/RhiMemoryBudget.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <span>

namespace Swim::RhiVulkan
{

	struct VulkanDeviceState;

	Rhi::MemoryBudgetSnapshot BuildVulkanMemoryBudgetSnapshot(const VkPhysicalDeviceMemoryProperties& properties,
		std::span<const VmaBudget> allocatorBudgets, const VkPhysicalDeviceMemoryBudgetPropertiesEXT* driverBudget);
	Rhi::MemoryBudgetSnapshot QueryVulkanMemoryBudget(const VulkanDeviceState& state);

} // namespace Swim::RhiVulkan
