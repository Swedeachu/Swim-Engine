#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	bool ValidateVulkanStorageTexture(const VulkanDeviceState& state, const Rhi::TextureDesc& desc);

} // namespace Swim::RhiVulkan
