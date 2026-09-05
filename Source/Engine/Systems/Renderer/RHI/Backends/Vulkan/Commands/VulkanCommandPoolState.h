#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <volk.h>

#include <cstdint>
#include <memory>

namespace Swim::RhiVulkan
{

		struct VulkanCommandPoolState
		{
			std::shared_ptr<VulkanDeviceState> DeviceState;
			VkCommandPool Pool = VK_NULL_HANDLE;
			std::uint32_t FamilyIndex = UINT32_MAX;
			std::uint64_t Generation = 0;

			~VulkanCommandPoolState()
			{
				if (Pool != VK_NULL_HANDLE)
				{
					DeviceState->Dispatch.vkDestroyCommandPool(DeviceState->Device.device, Pool, nullptr);
				}
			}
		};

} // namespace Swim::RhiVulkan
