#include "Tests/Fixtures/VulkanPipelineCacheCapture.h"

#include <algorithm>
#include <cstring>

namespace Swim::Testing
{

	namespace
	{
		VulkanPipelineCacheCapture* active = nullptr;
	}

	VulkanPipelineCacheCapture::VulkanPipelineCacheCapture()
	{
		active = this;
		State->Instance->Diagnostics.Echo = false;
		State->Instance->Diagnostics.Log = std::make_shared<Rhi::DiagnosticLog>();
		auto& properties = State->Device.physical_device.properties;
		properties.vendorID = 0x1234;
		properties.deviceID = 0x5678;
		properties.driverVersion = 42;
		Payload.resize(36);
		const std::uint32_t fields[]{ 32, 1, properties.vendorID, properties.deviceID };
		for (std::size_t field = 0; field < 4; ++field)
		{
			for (std::size_t byte = 0; byte < 4; ++byte)
			{
				Payload[field * 4 + byte] = static_cast<std::byte>((fields[field] >> (byte * 8)) & 255);
			}
		}
		for (std::size_t index = 0; index < VK_UUID_SIZE; ++index)
		{
			properties.pipelineCacheUUID[index] = static_cast<std::uint8_t>(index + 1);
			Payload[16 + index] = static_cast<std::byte>(index + 1);
		}
		Payload[32] = std::byte{0xde};
		Payload[33] = std::byte{0xad};
		Payload[34] = std::byte{0xbe};
		Payload[35] = std::byte{0xef};
		State->Dispatch.vkCreatePipelineCache = +[](VkDevice, const VkPipelineCacheCreateInfo* info, const VkAllocationCallbacks*, VkPipelineCache* cache) -> VkResult
		{
			++active->CachesCreated;
			active->Flags = info->flags;
			active->InitialData.clear();
			if (info->initialDataSize != 0)
			{
				const auto* bytes = static_cast<const std::byte*>(info->pInitialData);
				active->InitialData.assign(bytes, bytes + info->initialDataSize);
			}
			// Failure deliberately writes an unspecified non-null output.
			*cache = RhiVulkan::FromNativeHandle<VkPipelineCache>(100 + active->CachesCreated);
			return active->CreateResult;
		};
		State->Dispatch.vkDestroyPipelineCache = +[](VkDevice, VkPipelineCache, const VkAllocationCallbacks*) { ++active->CachesDestroyed; };
		State->Dispatch.vkGetPipelineCacheData = +[](VkDevice, VkPipelineCache, std::size_t* size, void* data) -> VkResult
		{
			if (data == nullptr)
			{
				++active->SizeCalls;
				*size = active->ReportedSize.value_or(active->Payload.size());
				return active->SizeResult;
			}
			++active->DataCalls;
			const auto written = std::min(*size, active->Payload.size());
			std::memcpy(data, active->Payload.data(), written);
			*size = active->WrittenSize.value_or(written);
			return active->DataResult;
		};
		Device = std::make_unique<RhiVulkan::VulkanDevice>(State, Rhi::AdapterInfo{}, nullptr, nullptr, nullptr);
	}

	VulkanPipelineCacheCapture::~VulkanPipelineCacheCapture()
	{
		RhiVulkan::DestroyVulkanPipelineCache(*State);
	}

	std::vector<std::byte> VulkanPipelineCacheCapture::EncodedData() const
	{
		return RhiVulkan::EncodeVulkanPipelineCacheData(Payload, State->Device.physical_device.properties);
	}

} // namespace Swim::Testing
