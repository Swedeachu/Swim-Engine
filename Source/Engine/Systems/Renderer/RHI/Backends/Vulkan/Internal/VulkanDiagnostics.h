#pragma once

#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"

#include <volk.h>
#include <VkBootstrap.h>

namespace Swim::RhiVulkan
{

	struct VulkanDeviceState;

	struct VulkanDiagnosticsState
	{
		std::shared_ptr<Rhi::DiagnosticLog> Log;
		bool Echo = true;
		bool ValidationEnabled = false;
		bool DebugUtilsEnabled = false;
	};

	struct VulkanDiagnosticsPolicy
	{
		bool Valid = false;
		bool Validation = false;
		bool DebugUtils = false;
	};

	VulkanDiagnosticsPolicy SelectDiagnosticsPolicy(Rhi::ValidationMode mode,
		bool debugDefault, bool layersAvailable, bool debugUtilsAvailable);
	bool ConfigureInstanceDiagnostics(vkb::InstanceBuilder& builder, VulkanDiagnosticsState& state,
		Rhi::ValidationMode mode, PFN_vkGetInstanceProcAddr getInstanceProcAddr);
	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDiagnosticCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) noexcept;
	void SetVulkanObjectName(const VulkanDeviceState& state, VkObjectType type,
		std::uint64_t handle, std::string_view name) noexcept;

} // namespace Swim::RhiVulkan
