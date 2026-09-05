#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"

namespace Swim::RhiVulkan
{

	VulkanDiagnosticsPolicy SelectDiagnosticsPolicy(Rhi::ValidationMode mode,
		bool debugDefault, bool layersAvailable, bool debugUtilsAvailable)
	{
		bool requested = false;
		switch (mode)
		{
		case Rhi::ValidationMode::Default:
			requested = debugDefault;
			break;
		case Rhi::ValidationMode::Disabled:
			break;
		case Rhi::ValidationMode::IfAvailable:
		case Rhi::ValidationMode::Required:
			requested = true;
			break;
		default:
			return {};
		}
		const bool available = layersAvailable && debugUtilsAvailable;
		return { mode != Rhi::ValidationMode::Required || available, requested && available, debugUtilsAvailable };
	}

	bool ConfigureInstanceDiagnostics(vkb::InstanceBuilder& builder, VulkanDiagnosticsState& state,
		Rhi::ValidationMode mode, PFN_vkGetInstanceProcAddr getInstanceProcAddr)
	{
		auto system = vkb::SystemInfo::get_system_info(getInstanceProcAddr);
		if (!system)
		{
			state.Log->Record(Rhi::DiagnosticSeverity::Error, "InstanceDiagnostics", "Failed to enumerate Vulkan instance capabilities");
			return false;
		}
#if defined(SWIM_VULKAN_VALIDATION)
		constexpr bool debugDefault = true;
#else
		constexpr bool debugDefault = false;
#endif
		const auto policy = SelectDiagnosticsPolicy(mode, debugDefault,
			system->validation_layers_available, system->debug_utils_available);
		if (!policy.Valid)
		{
			state.Log->Record(Rhi::DiagnosticSeverity::Error, "ValidationRequired",
				"Required Vulkan validation needs VK_LAYER_KHRONOS_validation and VK_EXT_debug_utils (or validation mode is invalid)");
			return false;
		}
		state.ValidationEnabled = policy.Validation;
		state.DebugUtilsEnabled = policy.DebugUtils;
		if (policy.Validation)
		{
			// Required after capability selection: never silently degrade if setup fails.
			builder.enable_validation_layers(true);
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
		state.Log->Record(Rhi::DiagnosticSeverity::Info, "Validation", policy.Validation ? "Vulkan validation enabled" : "Vulkan validation disabled");
		return true;
	}

} // namespace Swim::RhiVulkan
