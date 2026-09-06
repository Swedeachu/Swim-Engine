#include "Tests/Fixtures/VulkanUploadCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiUploadArena.h"

#include <array>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.UploadArena", "PersistentMappingSurvivesWritesFlushesAndReuse")
{
	Testing::VulkanUploadCapture capture;
	auto arena = Rhi::UploadArena::Create(*capture.Device, { 1024 });
	SWIM_REQUIRE(arena);
	SWIM_REQUIRE_EQUAL(capture.MapCalls, 1u);
	const std::array pattern{ std::byte{ 3 }, std::byte{ 7 }, std::byte{ 19 } };
	auto first = arena->Write(pattern);
	SWIM_REQUIRE(first);
	const auto* mapped = first->Resource->GetMappedWriteSpan().data();
	arena->Flush();
	SWIM_REQUIRE_EQUAL(capture.Flushes.size(), 1u);
	SWIM_CHECK_EQUAL(capture.Flushes[0].offset % 256, 0ull);
	SWIM_CHECK_EQUAL(capture.Flushes[0].size, 256ull);
	for (unsigned iteration = 0; iteration < 8; ++iteration)
	{
		arena->Reset();
		auto slice = arena->Write(pattern);
		SWIM_REQUIRE(slice);
		SWIM_CHECK(slice->Bytes.data() == mapped);
		arena->Flush();
	}
	// Compatibility Write uses the same mapping and maintains caches itself.
	first->Resource->Write(4, pattern);
	SWIM_CHECK(std::equal(pattern.begin(), pattern.end(), mapped + 4));
	SWIM_CHECK_EQUAL(capture.MapCalls, 1u);
	SWIM_CHECK_EQUAL(capture.UnmapCalls, 0u);
	SWIM_CHECK_EQUAL(capture.IdleCalls, 0u);
	arena.reset();
	SWIM_CHECK_EQUAL(capture.UnmapCalls, 1u);
	SWIM_CHECK_EQUAL(capture.CreateCalls, capture.DestroyCalls);
}

SWIM_TEST("RHI.Vulkan.UploadArena", "CoherentMemoryNeedsNoNativeFlush")
{
	Testing::VulkanUploadCapture capture(true);
	auto arena = Rhi::UploadArena::Create(*capture.Device, { 512 });
	SWIM_REQUIRE(arena);
	SWIM_REQUIRE(arena->Allocate(7));
	arena->Flush();
	SWIM_CHECK(capture.Flushes.empty());
	SWIM_CHECK_EQUAL(capture.MapCalls, 1u);
}

SWIM_TEST("RHI.Vulkan.UploadArena", "SeparateArenasDoNotShareNonCoherentAtoms")
{
	Testing::VulkanUploadCapture capture;
	auto first = Rhi::UploadArena::Create(*capture.Device, { 31 });
	auto second = Rhi::UploadArena::Create(*capture.Device, { 31 });
	SWIM_REQUIRE(first && second);
	SWIM_REQUIRE(first->Allocate(31));
	SWIM_REQUIRE(second->Allocate(31));
	first->Flush();
	second->Flush();
	SWIM_REQUIRE_EQUAL(capture.Flushes.size(), 2u);
	const auto& a = capture.Flushes[0];
	const auto& b = capture.Flushes[1];
	SWIM_CHECK(a.memory != b.memory || a.offset + a.size <= b.offset || b.offset + b.size <= a.offset);
}

SWIM_TEST("RHI.Vulkan.UploadArena", "MappingAndFlushContractsRejectInvalidRanges")
{
	Testing::VulkanUploadCapture capture;
	Rhi::BufferDesc desc{ 512, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::DeviceLocal, {}, true };
	SWIM_CHECK(!capture.Device->CreateBuffer(desc));
	desc.Memory = Rhi::MemoryPreference::GpuToCpu;
	SWIM_CHECK(!capture.Device->CreateBuffer(desc));
	SWIM_CHECK_EQUAL(capture.CreateCalls, 0u);
	desc.Memory = Rhi::MemoryPreference::CpuToGpu;
	desc.PersistentMap = false;
	auto regular = capture.Device->CreateBuffer(desc);
	SWIM_REQUIRE(regular);
	SWIM_CHECK(regular->GetMappedWriteSpan().empty());
	SWIM_CHECK_THROWS(regular->FlushMappedWrites(0, 1), std::invalid_argument);
	desc.PersistentMap = true;
	auto mapped = capture.Device->CreateBuffer(desc);
	SWIM_REQUIRE(mapped);
	SWIM_CHECK_THROWS(mapped->FlushMappedWrites(513, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(mapped->FlushMappedWrites(1, UINT64_MAX), std::invalid_argument);
	SWIM_CHECK_THROWS(mapped->FlushMappedWrites(UINT64_MAX, 1), std::invalid_argument);
	mapped->FlushMappedWrites(512, 0);
	SWIM_CHECK(capture.Flushes.empty());
	mapped->FlushMappedWrites(257, 1);
	SWIM_REQUIRE_EQUAL(capture.Flushes.size(), 1u);
	SWIM_CHECK_EQUAL(capture.Flushes[0].offset % 256, 0ull);
	SWIM_CHECK_EQUAL(capture.Flushes[0].size, 256ull);
}

SWIM_TEST("RHI.Vulkan.UploadArena", "MapFailureReleasesAllocationAndBuffer")
{
	Testing::VulkanUploadCapture capture;
	capture.MapResult = VK_ERROR_MEMORY_MAP_FAILED;
	SWIM_CHECK(!Rhi::UploadArena::Create(*capture.Device, { 1024 }));
	SWIM_CHECK(capture.MapCalls != 0);
	SWIM_CHECK_EQUAL(capture.CreateCalls, capture.DestroyCalls);
	SWIM_CHECK(capture.Buffers.empty());
	VmaTotalStatistics stats{};
	vmaCalculateStatistics(capture.State->Allocator, &stats);
	SWIM_CHECK_EQUAL(stats.total.statistics.allocationCount, 0u);
}

SWIM_TEST("RHI.Vulkan.UploadArena", "FlushErrorsRetainBytesAndDeviceLossPreventsFurtherWork")
{
	Testing::VulkanUploadCapture capture;
	auto arena = Rhi::UploadArena::Create(*capture.Device, { 32 });
	SWIM_REQUIRE(arena);
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	slice->Bytes[0] = std::byte{ 43 };
	capture.FlushResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_CHECK_THROWS(arena->Flush(), std::runtime_error);
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 4ull);
	SWIM_CHECK_EQUAL(slice->Bytes[0], std::byte{ 43 });
	capture.FlushResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(arena->Flush(), Rhi::DeviceLostError);
	const auto flushes = capture.Flushes.size();
	SWIM_CHECK_THROWS(arena->Flush(), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(arena->Allocate(4), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(slice->Resource->GetMappedWriteSpan(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.Flushes.size(), flushes);
}

SWIM_TEST("RHI.Vulkan.UploadArena", "KnownDeviceLossRejectsMappingAndCreationBeforeNativeCalls")
{
	Testing::VulkanUploadCapture capture;
	auto arena = Rhi::UploadArena::Create(*capture.Device, { 64 });
	SWIM_REQUIRE(arena);
	RhiVulkan::ObserveVulkanResult(*capture.State, VK_ERROR_DEVICE_LOST, "captured loss before upload");
	const auto creates = capture.CreateCalls;
	const auto maps = capture.MapCalls;
	SWIM_CHECK_THROWS(Rhi::UploadArena::Create(*capture.Device, { 64 }), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(arena->Allocate(4), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(arena->Flush(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.CreateCalls, creates);
	SWIM_CHECK_EQUAL(capture.MapCalls, maps);
	SWIM_CHECK(capture.Flushes.empty());
}
