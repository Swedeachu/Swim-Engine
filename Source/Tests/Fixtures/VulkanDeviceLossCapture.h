#pragma once

#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"

namespace Swim::Testing
{

	struct VulkanDeviceLossCapture : VulkanDescriptorCapture
	{
		VulkanDeviceLossCapture();
		~VulkanDeviceLossCapture();
		std::unique_ptr<RhiVulkan::VulkanDevice> Device;
		VkResult HostResult = VK_SUCCESS;
		std::uint32_t HostCalls = 0;
		std::uint32_t WaitCalls = 0;
		std::uint32_t QueryDestroys = 0;
		std::uint32_t CommandPoolDestroys = 0;
	};

} // namespace Swim::Testing
