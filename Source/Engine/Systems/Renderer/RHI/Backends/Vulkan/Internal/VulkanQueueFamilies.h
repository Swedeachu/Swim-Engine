#pragma once

// Queue-family selection for a candidate Vulkan physical device.

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <VkBootstrap.h>
#include <volk.h>

namespace Swim::RhiVulkan
{

	QueueFamilySelection SelectQueueFamilies(
		VkInstance instance,
		const vkb::PhysicalDevice& physicalDevice);

} // namespace Swim::RhiVulkan
