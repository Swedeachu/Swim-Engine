#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	bool SupportsVulkanDepthSampling(const VulkanDeviceState& state, Rhi::Format format);
	bool ValidateVulkanSampledDepthTexture(const VulkanDeviceState& state, const Rhi::TextureDesc& desc);

} // namespace Swim::RhiVulkan
