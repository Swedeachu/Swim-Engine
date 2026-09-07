#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanValidationSettings.h"

#include <cstring>
#include <string>
#include <vector>

namespace Swim::RhiVulkan
{

	namespace
	{
		template <typename Property, typename Enumerate>
		std::optional<std::vector<Property>> EnumerateProperties(
			Enumerate enumerate, Rhi::DiagnosticLog& log, const char* operation)
		{
			for (unsigned attempt = 0; attempt < 4; ++attempt)
			{
				std::uint32_t count = 0;
				auto result = enumerate(&count, nullptr);
				if (result == VK_INCOMPLETE)
				{
					continue;
				}
				if (result != VK_SUCCESS || count > 4096)
				{
					log.Record(Rhi::DiagnosticSeverity::Error, "ValidationCapabilities",
						std::string(operation) + " count failed or exceeded 4096; result=" + std::to_string(result));
					return {};
				}
				std::vector<Property> properties(count);
				if (count == 0)
				{
					return properties;
				}
				result = enumerate(&count, properties.data());
				if (result == VK_INCOMPLETE)
				{
					continue;
				}
				if (result != VK_SUCCESS || count > properties.size())
				{
					log.Record(Rhi::DiagnosticSeverity::Error, "ValidationCapabilities",
						std::string(operation) + " data failed or exceeded capacity; result=" + std::to_string(result));
					return {};
				}
				properties.resize(count);
				return properties;
			}
			log.Record(Rhi::DiagnosticSeverity::Error, "ValidationCapabilities",
				std::string(operation) + " did not stabilize after four attempts");
			return {};
		}

		bool Named(const char* name, const char* expected)
		{
			return std::strncmp(name, expected, VK_MAX_EXTENSION_NAME_SIZE) == 0;
		}
	}

	std::optional<VulkanValidationCapabilities> QueryValidationCapabilities(
		PFN_vkGetInstanceProcAddr getInstanceProcAddr, Rhi::DiagnosticLog& log)
	{
		if (!getInstanceProcAddr)
		{
			log.Record(Rhi::DiagnosticSeverity::Error, "ValidationCapabilities", "Vulkan instance procedure address is unavailable");
			return {};
		}
		const auto enumerateLayers = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
			getInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"));
		const auto enumerateExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
			getInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
		if (!enumerateLayers || !enumerateExtensions)
		{
			log.Record(Rhi::DiagnosticSeverity::Error, "ValidationCapabilities", "Vulkan instance enumeration is unavailable");
			return {};
		}
		const auto layers = EnumerateProperties<VkLayerProperties>(enumerateLayers, log, "vkEnumerateInstanceLayerProperties");
		if (!layers)
		{
			return {};
		}
		VulkanValidationCapabilities capabilities{};
		for (const auto& layer : *layers)
		{
			if (Named(layer.layerName, "VK_LAYER_KHRONOS_validation"))
			{
				capabilities.LayerAvailable = true;
				capabilities.LayerVersion = layer.specVersion;
				break;
			}
		}
		// An unrelated, disabled layer advertising an extension cannot satisfy
		// our requirements. Inspect only global and Khronos validation providers.
		for (unsigned provider = 0; provider < (capabilities.LayerAvailable ? 2u : 1u); ++provider)
		{
			const char* layer = provider == 0 ? nullptr : "VK_LAYER_KHRONOS_validation";
			const auto extensions = EnumerateProperties<VkExtensionProperties>(
				[&](std::uint32_t* count, VkExtensionProperties* properties)
				{
					return enumerateExtensions(layer, count, properties);
				}, log, provider == 0 ? "vkEnumerateInstanceExtensionProperties (global)" :
					"vkEnumerateInstanceExtensionProperties (validation layer)");
			if (!extensions)
			{
				return {};
			}
			for (const auto& extension : *extensions)
			{
				if (provider == 0)
				{
					capabilities.DebugUtilsAvailable |= Named(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				}
				else
				{
					capabilities.LayerDebugUtilsAvailable |= Named(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				}
				capabilities.LayerSettingsAvailable |= Named(extension.extensionName, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
			}
		}
		return capabilities;
	}

} // namespace Swim::RhiVulkan
