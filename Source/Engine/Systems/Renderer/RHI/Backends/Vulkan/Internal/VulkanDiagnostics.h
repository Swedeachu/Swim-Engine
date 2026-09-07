#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanValidationSettings.h"

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
		Rhi::ValidationChecks Checks{};
	};

	bool ConfigureInstanceDiagnostics(vkb::InstanceBuilder& builder, VulkanDiagnosticsState& state,
		const Rhi::GraphicsSystemDesc& desc, PFN_vkGetInstanceProcAddr getInstanceProcAddr);
	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDiagnosticCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) noexcept;
	void SetVulkanObjectName(const VulkanDeviceState& state, VkObjectType type,
		std::uint64_t handle, std::string_view name) noexcept;

} // namespace Swim::RhiVulkan
