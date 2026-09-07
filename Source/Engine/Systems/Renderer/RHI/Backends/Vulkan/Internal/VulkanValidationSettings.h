#pragma once

#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"

#include <volk.h>
#include <array>
#include <optional>

namespace Swim::RhiVulkan
{

	// Modern, dedicated setting names are the supported contract. Basic
	// validation remains usable with older layers and without layer settings.
	inline constexpr std::uint32_t MinimumValidationSettingsVersion = VK_MAKE_API_VERSION(0, 1, 4, 335);

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
	std::array<VkLayerSettingEXT, 3> GetVulkanValidationSettings(const Rhi::ValidationChecks& checks);
	VkPhysicalDeviceFeatures GetValidationDeviceFeatures(const Rhi::ValidationChecks& checks);

} // namespace Swim::RhiVulkan
