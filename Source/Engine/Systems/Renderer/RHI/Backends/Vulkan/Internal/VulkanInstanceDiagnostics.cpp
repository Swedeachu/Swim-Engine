#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"

namespace Swim::RhiVulkan
{

	bool ConfigureInstanceDiagnostics(vkb::InstanceBuilder& builder, VulkanDiagnosticsState& state,
		const Rhi::GraphicsSystemDesc& desc, PFN_vkGetInstanceProcAddr getInstanceProcAddr)
	{
		const auto capabilities = QueryValidationCapabilities(getInstanceProcAddr, *state.Log);
		if (!capabilities)
		{
			return false;
		}
#if defined(SWIM_VULKAN_VALIDATION)
		constexpr bool debugDefault = true;
#else
		constexpr bool debugDefault = false;
#endif
		const auto policy = SelectDiagnosticsPolicy(desc.Validation, debugDefault, *capabilities, desc.Checks);
		if (!policy.Valid)
		{
			std::string failure = std::string(policy.Failure) + "; detected layer API=" +
				std::to_string(VK_API_VERSION_MAJOR(capabilities->LayerVersion)) + "." +
				std::to_string(VK_API_VERSION_MINOR(capabilities->LayerVersion)) + "." +
				std::to_string(VK_API_VERSION_PATCH(capabilities->LayerVersion)) +
				"; VK_EXT_layer_settings=" + (capabilities->LayerSettingsAvailable ? "available" : "missing");
			if (desc.Validation != Rhi::ValidationMode::Disabled)
			{
				failure += ". Select a compatible Vulkan SDK validation layer with VK_LAYER_PATH (the GPU driver version is independent).";
			}
			state.Log->Record(Rhi::DiagnosticSeverity::Error, "ValidationRequired", failure);
			return false;
		}
		state.ValidationEnabled = policy.Validation;
		state.DebugUtilsEnabled = policy.DebugUtils;
		state.Checks = policy.Checks;
		if (policy.Validation)
		{
			// Required after capability selection: never silently degrade if setup fails.
			builder.enable_validation_layers(true);
		}
		if (policy.Checks.Any())
		{
			// vk-bootstrap adds VK_EXT_layer_settings and the create-info chain.
			// Values have static lifetime; no pointers into this stack escape.
			for (const auto& setting : GetVulkanValidationSettings(policy.Checks, capabilities->LayerVersion))
			{
				builder.add_layer_setting(setting);
			}
		}
		if (policy.DebugUtils)
		{
			builder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
				.set_debug_callback(&VulkanDiagnosticCallback)
				.set_debug_callback_user_data_pointer(&state)
				.set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				.set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
		}
		state.Log->Record(Rhi::DiagnosticSeverity::Info, "Validation",
			std::string(policy.Validation ? "Vulkan validation configured" : "Vulkan validation disabled") +
			"; core=" + (policy.Validation && !policy.Checks.GpuAssisted ? "on" : "off") +
			"; synchronization=" + (policy.Checks.Synchronization ? "on" : "off") +
			"; gpu-assisted=" + (policy.Checks.GpuAssisted ? "on" : "off") +
			"; layer API=" + std::to_string(VK_API_VERSION_MAJOR(capabilities->LayerVersion)) + "." +
			std::to_string(VK_API_VERSION_MINOR(capabilities->LayerVersion)) + "." +
			std::to_string(VK_API_VERSION_PATCH(capabilities->LayerVersion)));
		return true;
	}

} // namespace Swim::RhiVulkan
