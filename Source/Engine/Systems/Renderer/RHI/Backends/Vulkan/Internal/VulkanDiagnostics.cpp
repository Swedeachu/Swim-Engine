#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <cstdio>

namespace Swim::RhiVulkan
{

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDiagnosticCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) noexcept
	{
		auto* state = static_cast<VulkanDiagnosticsState*>(userData);
		if (state == nullptr)
		{
			return VK_FALSE;
		}
		const auto level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0
			? Rhi::DiagnosticSeverity::Error
			: (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0
				? Rhi::DiagnosticSeverity::Warning : Rhi::DiagnosticSeverity::Info;
		const char* id = data && data->pMessageIdName ? data->pMessageIdName : "Vulkan";
		const char* message = data && data->pMessage ? data->pMessage : "No diagnostic text supplied";
		if (state->Log)
		{
			state->Log->Record(level, id, message);
		}
		if (state->Echo)
		{
			std::fprintf(stderr, "[Swim Vulkan] %s: %s\n", id, message);
		}
		// Never throw, call Vulkan, invoke arbitrary user code, or ask validation
		// to skip the originating operation from this callback.
		return VK_FALSE;
	}

	void SetVulkanObjectName(const VulkanDeviceState& state, VkObjectType type,
		std::uint64_t handle, std::string_view name) noexcept
	{
		if (state.Diagnostics->IsLost() || !state.Instance || !state.Instance->Diagnostics.DebugUtilsEnabled || handle == 0 || name.empty() ||
			state.Instance->Dispatch.vkSetDebugUtilsObjectNameEXT == nullptr)
		{
			return;
		}
		try
		{
			const std::string owned(name);
			VkDebugUtilsObjectNameInfoEXT info{};
			info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
			info.objectType = type;
			info.objectHandle = handle;
			info.pObjectName = owned.c_str();
			const auto result = ObserveVulkanResult(state, state.Instance->Dispatch.vkSetDebugUtilsObjectNameEXT(state.Device.device, &info), "vkSetDebugUtilsObjectNameEXT");
			if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST && state.Instance->Diagnostics.Log)
			{
				state.Instance->Diagnostics.Log->Record(Rhi::DiagnosticSeverity::Warning,
					"ObjectName", "Vulkan object naming failed");
			}
		}
		catch (...)
		{
			if (state.Instance->Diagnostics.Log)
			{
				state.Instance->Diagnostics.Log->Record(Rhi::DiagnosticSeverity::Warning,
					"ObjectName", "Could not retain Vulkan object name");
			}
		}
	}

} // namespace Swim::RhiVulkan
