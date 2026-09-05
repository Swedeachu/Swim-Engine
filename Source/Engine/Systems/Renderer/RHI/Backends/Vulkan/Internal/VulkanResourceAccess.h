#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <stdexcept>

namespace Swim::RhiVulkan
{

	template <typename VulkanResource, typename Resource>
	VulkanResource& RequireResource(Resource& resource, const std::shared_ptr<VulkanDeviceState>& state)
	{
		auto* native = dynamic_cast<VulkanResource*>(&resource);
		if (native == nullptr || native->GetState().get() != state.get())
		{
			throw std::invalid_argument("Vulkan commands require resources from the same Vulkan device");
		}
		return *native;
	}

} // namespace Swim::RhiVulkan
