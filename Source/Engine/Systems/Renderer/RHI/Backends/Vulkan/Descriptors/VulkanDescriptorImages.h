#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	VkDescriptorImageInfo BuildVulkanImageDescriptor(const std::shared_ptr<VulkanDeviceState>& state,
		const Rhi::DescriptorBindingDesc& binding, const Rhi::DescriptorWrite& write);

} // namespace Swim::RhiVulkan
