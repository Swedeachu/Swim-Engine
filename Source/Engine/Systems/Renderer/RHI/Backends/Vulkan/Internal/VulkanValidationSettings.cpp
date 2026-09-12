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
		if (checks.GpuAssisted && capabilities.LayerVersion < MinimumGpuValidationSettingsVersion)
		{
			return { false, false, false, {}, "GPU-assisted validation needs Khronos validation layer 1.4.350 or newer for independently configurable GPU checks" };
		}
		const bool validation = requested && available;
		return { true, validation, capabilities.DebugUtilsAvailable ||
			(validation && capabilities.LayerDebugUtilsAvailable), checks, nullptr };
	}

	std::vector<VkLayerSettingEXT> GetVulkanValidationSettings(const Rhi::ValidationChecks& checks, std::uint32_t layerVersion)
	{
		static constexpr VkBool32 enabled = VK_TRUE;
		static constexpr VkBool32 disabled = VK_FALSE;
		std::vector<VkLayerSettingEXT> settings{
			{ "VK_LAYER_KHRONOS_validation", "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, checks.Synchronization ? &enabled : &disabled },
			{ "VK_LAYER_KHRONOS_validation", "gpuav_enable", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, checks.GpuAssisted ? &enabled : &disabled },
			{ "VK_LAYER_KHRONOS_validation", "syncval_submit_time_validation", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, &enabled },
			// GPU-AV is a separate pass after core validation, as recommended
			// by Khronos. Combining both emits a performance warning.
			{ "VK_LAYER_KHRONOS_validation", "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT,
				1, checks.GpuAssisted ? &disabled : &enabled },
		};
		if (checks.GpuAssisted)
		{
			// These APIs are not enabled by Swim. Keep validation for every
			// supported graphics/compute operation, without requesting checks
			// that the layer must disable at device creation.
			for (const char* name : { "gpuav_validate_trace_ray", "gpuav_mesh_shading" })
			{
				settings.push_back({ "VK_LAYER_KHRONOS_validation", name, VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &disabled });
			}
			// 1.4.357 merged ray-query validation into the trace-ray setting.
			if (layerVersion < VK_MAKE_API_VERSION(0, 1, 4, 357))
			{
				settings.push_back({ "VK_LAYER_KHRONOS_validation", "gpuav_validate_ray_query",
					VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &disabled });
			}
		}
		return settings;
	}

	VkPhysicalDeviceFeatures GetValidationDeviceFeatures(const Rhi::ValidationChecks& checks)
	{
		VkPhysicalDeviceFeatures features{};
		if (checks.GpuAssisted)
		{
			features.fragmentStoresAndAtomics = VK_TRUE;
			features.vertexPipelineStoresAndAtomics = VK_TRUE;
			features.shaderInt64 = VK_TRUE;
			features.shaderInt16 = VK_TRUE;
		}
		// Vulkan 1.3, timeline semaphores and buffer device address are already
		// part of Swim's baseline. The layer reserves its descriptor slot and
		// exposes the reduced maxBoundDescriptorSets to normal layout validation.
		return features;
	}

	VkPhysicalDeviceVulkan11Features GetValidationVulkan11Features(const Rhi::ValidationChecks& checks)
	{
		VkPhysicalDeviceVulkan11Features features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		features.storageBuffer16BitAccess = checks.GpuAssisted ? VK_TRUE : VK_FALSE;
		return features;
	}

	VkPhysicalDeviceVulkan12Features GetValidationVulkan12Features(const Rhi::ValidationChecks& checks)
	{
		VkPhysicalDeviceVulkan12Features features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		if (checks.GpuAssisted)
		{
			features.vulkanMemoryModel = VK_TRUE;
			features.vulkanMemoryModelDeviceScope = VK_TRUE;
			features.scalarBlockLayout = VK_TRUE;
			features.storageBuffer8BitAccess = VK_TRUE;
			features.shaderInt8 = VK_TRUE;
		}
		return features;
	}

} // namespace Swim::RhiVulkan
