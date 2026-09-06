#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"

#include <unordered_map>
#include <vector>

namespace Swim::Testing
{

	// Real VMA allocation/mapping/flush logic over host-owned fake device memory.
	struct VulkanUploadCapture
	{
		explicit VulkanUploadCapture(bool coherent = false);
		~VulkanUploadCapture();

		std::shared_ptr<RhiVulkan::VulkanDeviceState> State = std::make_shared<RhiVulkan::VulkanDeviceState>();
		std::unique_ptr<RhiVulkan::VulkanDevice> Device;
		bool Coherent = false;
		VkResult MapResult = VK_SUCCESS;
		VkResult FlushResult = VK_SUCCESS;
		std::uint32_t MapCalls = 0;
		std::uint32_t UnmapCalls = 0;
		std::uint32_t CreateCalls = 0;
		std::uint32_t DestroyCalls = 0;
		std::uint32_t IdleCalls = 0;
		std::uintptr_t NextMemory = 1;
		std::unordered_map<std::uintptr_t, std::vector<std::byte>> Memory;
		std::unordered_map<std::uintptr_t, VkDeviceSize> Buffers;
		std::vector<VkMappedMemoryRange> Flushes;
	};

} // namespace Swim::Testing
