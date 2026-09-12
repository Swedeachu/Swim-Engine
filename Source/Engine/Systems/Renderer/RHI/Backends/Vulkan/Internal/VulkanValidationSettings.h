#pragma once

#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"

#include <volk.h>
#include <array>
#include <optional>
#include <vector>

namespace Swim::RhiVulkan
{

	// Modern, dedicated setting names are the supported contract. Basic
	// validation remains usable with older layers and without layer settings.
	inline constexpr std::uint32_t MinimumValidationSettingsVersion = VK_MAKE_API_VERSION(0, 1, 4, 335);
	// 1.4.341 enables a ray-hit-object checker without exposing a setting to
	// disable it for devices that do not enable ray tracing. 1.4.350 provides
	// the independently configurable GPU checks used by this backend.
	inline constexpr std::uint32_t MinimumGpuValidationSettingsVersion = VK_MAKE_API_VERSION(0, 1, 4, 350);

	struct VulkanValidationCapabilities
	{
		bool LayerAvailable = false;
		bool DebugUtilsAvailable = false; // Global provider; usable without validation.
		bool LayerSettingsAvailable = false;
		std::uint32_t LayerVersion = 0;
		bool LayerDebugUtilsAvailable = false;
	};

	struct VulkanDiagnosticsPolicy
	{
		bool Valid = false;
		bool Validation = false;
		bool DebugUtils = false;
		Rhi::ValidationChecks Checks{};
		const char* Failure = "Invalid validation mode";
	};

	std::optional<VulkanValidationCapabilities> QueryValidationCapabilities(
		PFN_vkGetInstanceProcAddr getInstanceProcAddr, Rhi::DiagnosticLog& log);
	VulkanDiagnosticsPolicy SelectDiagnosticsPolicy(Rhi::ValidationMode mode, bool debugDefault,
		const VulkanValidationCapabilities& capabilities, const Rhi::ValidationChecks& checks = {});
	// The setting value pointers refer to immutable static storage and remain
	// valid through deferred vk-bootstrap instance creation and builder copies.
	std::vector<VkLayerSettingEXT> GetVulkanValidationSettings(const Rhi::ValidationChecks& checks,
		std::uint32_t layerVersion = MinimumGpuValidationSettingsVersion);
	VkPhysicalDeviceFeatures GetValidationDeviceFeatures(const Rhi::ValidationChecks& checks);
	VkPhysicalDeviceVulkan11Features GetValidationVulkan11Features(const Rhi::ValidationChecks& checks);
	VkPhysicalDeviceVulkan12Features GetValidationVulkan12Features(const Rhi::ValidationChecks& checks);

} // namespace Swim::RhiVulkan
