#include "Tests/Fixtures/VulkanValidationCapture.h"

#include <algorithm>
#include <cstring>

namespace Swim::Testing
{

	namespace
	{
		VulkanValidationCapture* active = nullptr;

		template <typename Property>
		VkResult Enumerate(unsigned stage, const std::vector<Property>& source, std::uint32_t* count, Property* data)
		{
			if (!data)
			{
				++active->CountCalls[stage];
				*count = active->CountOverride[stage] ? active->CountOverride[stage] : static_cast<std::uint32_t>(source.size());
				return active->CountResult[stage];
			}
			++active->DataCalls[stage];
			if (active->DataCalls[stage] <= active->IncompleteDataCalls[stage])
			{
				return VK_INCOMPLETE;
			}
			if (active->OversizedDataCount[stage])
			{
				++*count;
				return VK_SUCCESS;
			}
			*count = std::min(*count, active->Shrink[stage] ? 1u : static_cast<std::uint32_t>(source.size()));
			std::copy_n(source.data(), *count, data);
			return active->DataResult[stage];
		}
	}

	VulkanValidationCapture::VulkanValidationCapture()
	{
		active = this;
		VkLayerProperties validation{};
		std::strcpy(validation.layerName, "VK_LAYER_KHRONOS_validation");
		validation.specVersion = VK_MAKE_API_VERSION(0, 1, 4, 350);
		VkLayerProperties unrelated{};
		std::strcpy(unrelated.layerName, "VK_LAYER_other");
		Layers = { validation, unrelated };
		VkExtensionProperties debug{};
		std::strcpy(debug.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		VkExtensionProperties settings{};
		std::strcpy(settings.extensionName, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
		GlobalExtensions = { debug };
		ValidationExtensions = { settings };
	}

	VulkanValidationCapture::~VulkanValidationCapture()
	{
		active = nullptr;
	}

	std::optional<RhiVulkan::VulkanValidationCapabilities> VulkanValidationCapture::Query()
	{
		return RhiVulkan::QueryValidationCapabilities(&GetInstanceProcAddress, Log);
	}

	VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL VulkanValidationCapture::GetInstanceProcAddress(VkInstance instance, const char* name)
	{
		active->NonNullInstance |= instance != VK_NULL_HANDLE;
		if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0 && !active->MissingLayersFunction)
		{
			return reinterpret_cast<PFN_vkVoidFunction>(&EnumerateLayers);
		}
		if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0 && !active->MissingExtensionsFunction)
		{
			return reinterpret_cast<PFN_vkVoidFunction>(&EnumerateExtensions);
		}
		return nullptr;
	}

	VKAPI_ATTR VkResult VKAPI_CALL VulkanValidationCapture::EnumerateLayers(std::uint32_t* count, VkLayerProperties* properties)
	{
		return Enumerate(0, active->Layers, count, properties);
	}

	VKAPI_ATTR VkResult VKAPI_CALL VulkanValidationCapture::EnumerateExtensions(
		const char* layer, std::uint32_t* count, VkExtensionProperties* properties)
	{
		if (layer != nullptr && std::strcmp(layer, "VK_LAYER_KHRONOS_validation") != 0)
		{
			active->UnexpectedProvider = true;
			return VK_ERROR_LAYER_NOT_PRESENT;
		}
		return Enumerate(layer ? 2 : 1, layer ? active->ValidationExtensions : active->GlobalExtensions, count, properties);
	}

} // namespace Swim::Testing
