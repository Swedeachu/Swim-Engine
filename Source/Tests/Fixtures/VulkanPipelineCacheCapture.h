#pragma once

#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineCacheData.h"

#include <optional>

namespace Swim::Testing
{

	struct VulkanPipelineCacheCapture : VulkanPipelineCapture
	{
		VulkanPipelineCacheCapture();
		~VulkanPipelineCacheCapture();
		std::vector<std::byte> EncodedData() const;

		std::unique_ptr<RhiVulkan::VulkanDevice> Device;
		std::vector<std::byte> Payload;
		std::vector<std::byte> InitialData;
		VkPipelineCacheCreateFlags Flags = UINT32_MAX;
		VkResult CreateResult = VK_SUCCESS;
		VkResult SizeResult = VK_SUCCESS;
		VkResult DataResult = VK_SUCCESS;
		std::optional<std::size_t> ReportedSize;
		std::optional<std::size_t> WrittenSize;
		std::uint32_t SizeCalls = 0;
		std::uint32_t DataCalls = 0;
	};

} // namespace Swim::Testing
