#pragma once

#include "Engine/Systems/Renderer/RHI/RhiDeviceDiagnostics.h"

#include <volk.h>

namespace Swim::RhiVulkan
{

	struct VulkanDeviceState;

	// Only the first VK_ERROR_DEVICE_LOST collects optional fault information.
	// Observation never throws; use it in teardown and other noexcept paths.
	VkResult ObserveVulkanResult(const VulkanDeviceState& state, VkResult result, std::string_view operation) noexcept;
	VkResult CheckVulkanResult(const VulkanDeviceState& state, VkResult result, std::string_view operation);
	void RequireVulkanDevice(const VulkanDeviceState& state);
	void WaitForVulkanDeviceIdle(const VulkanDeviceState& state);
	void RetireLostVulkanDevice(const VulkanDeviceState& state) noexcept;

	VkPhysicalDeviceFaultFeaturesEXT QueryDeviceFaultFeatures(const volk::VolkInstanceTable& dispatch,
		VkPhysicalDevice physicalDevice, bool extensionAvailable, bool requested);
	Rhi::DeviceFaultDetails CaptureVulkanDeviceFault(const VulkanDeviceState& state) noexcept;

} // namespace Swim::RhiVulkan
