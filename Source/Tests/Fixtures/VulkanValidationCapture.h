#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanValidationSettings.h"

#include <array>
#include <vector>

namespace Swim::Testing
{

	// Captures loader enumeration directly; never initializes vk-bootstrap's
	// process-global loader or substitutes the dispatch used by desktop smokes.
	class VulkanValidationCapture
	{
	public:
		VulkanValidationCapture();
		~VulkanValidationCapture();
		VulkanValidationCapture(const VulkanValidationCapture&) = delete;
		VulkanValidationCapture& operator=(const VulkanValidationCapture&) = delete;

		std::vector<VkLayerProperties> Layers;
		std::vector<VkExtensionProperties> GlobalExtensions;
		std::vector<VkExtensionProperties> ValidationExtensions;
		// Stages: layer list, global extensions, validation layer extensions.
		std::array<VkResult, 3> CountResult{ VK_SUCCESS, VK_SUCCESS, VK_SUCCESS };
		std::array<VkResult, 3> DataResult{ VK_SUCCESS, VK_SUCCESS, VK_SUCCESS };
		std::array<unsigned, 3> CountCalls{};
		std::array<unsigned, 3> DataCalls{};
		std::array<unsigned, 3> IncompleteDataCalls{};
		std::array<std::uint32_t, 3> CountOverride{};
		std::array<bool, 3> Shrink{};
		std::array<bool, 3> OversizedDataCount{};
		bool MissingLayersFunction = false;
		bool MissingExtensionsFunction = false;
		bool UnexpectedProvider = false;
		bool NonNullInstance = false;

		Rhi::DiagnosticLog Log;
		std::optional<RhiVulkan::VulkanValidationCapabilities> Query();
		static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddress(VkInstance instance, const char* name);

	private:
		static VKAPI_ATTR VkResult VKAPI_CALL EnumerateLayers(std::uint32_t* count, VkLayerProperties* properties);
		static VKAPI_ATTR VkResult VKAPI_CALL EnumerateExtensions(const char* layer, std::uint32_t* count, VkExtensionProperties* properties);
	};

} // namespace Swim::Testing
