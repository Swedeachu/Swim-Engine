#include "Tests/Fixtures/VulkanPipelineCacheCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.PipelineCacheData", "EnvelopeRoundTripsExactPayloadFromUnalignedStorage")
{
	Testing::VulkanPipelineCacheCapture capture;
	auto bytes = capture.EncodedData();
	SWIM_CHECK_EQUAL(bytes.size(), RhiVulkan::VulkanPipelineCacheEnvelopeBytes + capture.Payload.size());
	SWIM_CHECK_EQUAL(std::to_integer<unsigned>(bytes[12]), 0x34u);
	SWIM_CHECK_EQUAL(std::to_integer<unsigned>(bytes[13]), 0x12u);
	SWIM_CHECK_EQUAL(std::to_integer<unsigned>(bytes[20]), 42u);
	SWIM_CHECK_EQUAL(std::to_integer<unsigned>(bytes[40]), 36u);
	bytes.insert(bytes.begin(), std::byte{0});
	std::span<const std::byte> payload;
	SWIM_CHECK(RhiVulkan::DecodeVulkanPipelineCacheData(std::span(bytes).subspan(1),
		capture.State->Device.physical_device.properties, payload) == Rhi::PipelineCacheLoadStatus::Loaded);
	SWIM_CHECK(std::equal(payload.begin(), payload.end(), capture.Payload.begin(), capture.Payload.end()));
}

SWIM_TEST("RHI.Vulkan.PipelineCacheData", "CorruptionTruncationTrailingBytesAndOversizeNeverReachTheDriver")
{
	Testing::VulkanPipelineCacheCapture capture;
	const auto valid = capture.EncodedData();
	for (std::size_t index = 0; index < valid.size(); ++index)
	{
		auto corrupt = valid;
		corrupt[index] ^= std::byte{1};
		SWIM_CHECK(capture.Device->LoadPipelineCache(corrupt) == Rhi::PipelineCacheLoadStatus::InvalidData);
	}
	for (std::size_t size = 1; size < valid.size(); ++size)
	{
		SWIM_CHECK(capture.Device->LoadPipelineCache(std::span(valid).first(size)) == Rhi::PipelineCacheLoadStatus::InvalidData);
	}
	auto trailing = valid;
	trailing.push_back(std::byte{});
	SWIM_CHECK(capture.Device->LoadPipelineCache(trailing) == Rhi::PipelineCacheLoadStatus::InvalidData);
	std::vector<std::byte> tooLarge(Rhi::MaxPipelineCacheDataBytes + 1);
	SWIM_CHECK(capture.Device->LoadPipelineCache(tooLarge) == Rhi::PipelineCacheLoadStatus::InvalidData);
	SWIM_CHECK(capture.Device->LoadPipelineCache(capture.Payload) == Rhi::PipelineCacheLoadStatus::InvalidData);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.PipelineCacheData", "DifferentDeviceDriverAndUuidRejectOtherwiseValidData")
{
	Testing::VulkanPipelineCacheCapture capture;
	const auto bytes = capture.EncodedData();
	const auto original = capture.State->Device.physical_device.properties;
	for (unsigned change = 0; change < 4; ++change)
	{
		auto& properties = capture.State->Device.physical_device.properties;
		properties = original;
		if (change == 0)
		{
			++properties.vendorID;
		}
		if (change == 1)
		{
			++properties.deviceID;
		}
		if (change == 2)
		{
			++properties.driverVersion;
		}
		if (change == 3)
		{
			++properties.pipelineCacheUUID[0];
		}
		SWIM_CHECK(capture.Device->LoadPipelineCache(bytes) == Rhi::PipelineCacheLoadStatus::Incompatible);
	}
	SWIM_CHECK_EQUAL(capture.CachesCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.PipelineCacheData", "InvalidNativeHeadersCannotBeExportedAndDecodeClearsOldViews")
{
	Testing::VulkanPipelineCacheCapture capture;
	const auto& properties = capture.State->Device.physical_device.properties;
	for (auto offset : { 0u, 4u, 8u, 12u, 16u })
	{
		auto payload = capture.Payload;
		payload[offset] ^= std::byte{1};
		SWIM_CHECK(!RhiVulkan::IsCompatibleVulkanPipelineCacheHeader(payload, properties));
		SWIM_CHECK_THROWS(RhiVulkan::EncodeVulkanPipelineCacheData(payload, properties), std::invalid_argument);
	}
	SWIM_CHECK(!RhiVulkan::IsCompatibleVulkanPipelineCacheHeader(std::span(capture.Payload).first(31), properties));
	std::span<const std::byte> view = capture.Payload;
	SWIM_CHECK(RhiVulkan::DecodeVulkanPipelineCacheData({}, properties, view) == Rhi::PipelineCacheLoadStatus::Empty);
	SWIM_CHECK(view.empty());
	view = capture.Payload;
	SWIM_CHECK(RhiVulkan::DecodeVulkanPipelineCacheData(capture.Payload, properties, view) == Rhi::PipelineCacheLoadStatus::InvalidData);
	SWIM_CHECK(view.empty());
}
