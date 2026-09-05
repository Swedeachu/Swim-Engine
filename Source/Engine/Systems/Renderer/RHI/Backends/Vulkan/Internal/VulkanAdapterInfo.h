#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	Rhi::AdapterInfo BuildAdapterInfo(const volk::VolkInstanceTable& dispatch, const vkb::PhysicalDevice& physicalDevice);
	void ReportAdapterInfo(VulkanDiagnosticsState& diagnostics, const Rhi::AdapterInfo& info);

} // namespace Swim::RhiVulkan
