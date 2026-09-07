#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanValidationSettings.h"

namespace Swim::RhiVulkan
{

	VulkanDiagnosticsPolicy SelectDiagnosticsPolicy(Rhi::ValidationMode mode, bool debugDefault,
		const VulkanValidationCapabilities& capabilities, const Rhi::ValidationChecks& checks)
	{
		bool requested = checks.Any();
		switch (mode)
		{
		case Rhi::ValidationMode::Default:
			requested |= debugDefault;
			break;
		case Rhi::ValidationMode::Disabled:
			if (checks.Any())
			{
				return { false, false, false, {}, "Validation checks cannot be requested with validation disabled" };
			}
			break;
		case Rhi::ValidationMode::IfAvailable:
		case Rhi::ValidationMode::Required:
			requested = true;
			break;
		default:
			return {};
		}
		const bool available = capabilities.LayerAvailable &&
			(capabilities.DebugUtilsAvailable || capabilities.LayerDebugUtilsAvailable);
		if ((mode == Rhi::ValidationMode::Required || checks.Any()) && !available)
		{
			return { false, false, false, {}, "Requested Vulkan validation needs VK_LAYER_KHRONOS_validation and VK_EXT_debug_utils" };
		}
		if (checks.Any() && (!capabilities.LayerSettingsAvailable ||
			capabilities.LayerVersion < MinimumValidationSettingsVersion))
		{
			return { false, false, false, {}, "Explicit validation checks need VK_EXT_layer_settings and Khronos validation layer 1.4.335 or newer" };
		}
		const bool validation = requested && available;
		return { true, validation, capabilities.DebugUtilsAvailable ||
			(validation && capabilities.LayerDebugUtilsAvailable), checks, nullptr };
	}

	std::array<VkLayerSettingEXT, 3> GetVulkanValidationSettings(const Rhi::ValidationChecks& checks)
	{
		static constexpr VkBool32 enabled = VK_TRUE;
		static constexpr VkBool32 disabled = VK_FALSE;
		return {{
			{ "VK_LAYER_KHRONOS_validation", "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, checks.Synchronization ? &enabled : &disabled },
			{ "VK_LAYER_KHRONOS_validation", "gpuav_enable", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, checks.GpuAssisted ? &enabled : &disabled },
			{ "VK_LAYER_KHRONOS_validation", "syncval_submit_time_validation", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, &enabled },
		}};
	}

	VkPhysicalDeviceFeatures GetValidationDeviceFeatures(const Rhi::ValidationChecks& checks)
	{
		VkPhysicalDeviceFeatures features{};
		if (checks.GpuAssisted)
		{
			features.fragmentStoresAndAtomics = VK_TRUE;
			features.vertexPipelineStoresAndAtomics = VK_TRUE;
		}
		// Vulkan 1.3, timeline semaphores and buffer device address are already
		// part of Swim's baseline. The layer reserves its descriptor slot and
		// exposes the reduced maxBoundDescriptorSets to normal layout validation.
		return features;
	}

} // namespace Swim::RhiVulkan
