#include "Tests/Fixtures/VulkanMappedBufferCapture.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <array>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.ReadbackArena", "PersistentReadsInvalidateAfterCompletionAndReuseMapping")
{
	Testing::VulkanMappedBufferCapture capture;
	Testing::MockDevice frameDevice; // Captured completion, real VMA host logic.
	auto frames = Rhi::FrameContextRing::Create(frameDevice, { Rhi::QueueType::Graphics, 1 });
	auto arena = Rhi::ReadbackArena::Create(*capture.Device, { 512 });
	SWIM_REQUIRE(arena);
	SWIM_REQUIRE_EQUAL(capture.MapCalls, 1u);
	for (unsigned iteration = 0; iteration < 3; ++iteration)
	{
		SWIM_REQUIRE(arena->Allocate(3));
		auto slice = arena->Allocate(8, 256);
		SWIM_REQUIRE(slice);
		SWIM_CHECK_EQUAL(slice->GetOffset(), 256ull);
		SWIM_CHECK(slice->GetBuffer().GetMappedWriteSpan().empty());
		const auto mapped = slice->GetBuffer().GetMappedReadSpan();
		// Simulate device writes in the capture's host-owned memory.
		std::fill_n(const_cast<std::byte*>(mapped.data()) + slice->GetOffset(), 8, static_cast<std::byte>(iteration + 1));
		frames->BeginFrame();
		std::array batches{ arena.get() };
		const auto completion = frames->SubmitCurrent(batches);
		std::array<std::byte, 8> output{};
		SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::NotReady);
		SWIM_CHECK_EQUAL(capture.Invalidations.size(), iteration);
		frameDevice.LastTimeline->Complete(completion);
		SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::Ready);
		SWIM_CHECK(std::all_of(output.begin(), output.end(), [iteration](std::byte byte) { return byte == static_cast<std::byte>(iteration + 1); }));
		SWIM_CHECK_EQUAL(capture.Invalidations.size(), iteration + 1u);
		SWIM_CHECK_EQUAL(capture.Invalidations.back().offset % 256, 0ull);
		SWIM_CHECK_EQUAL(capture.Invalidations.back().size, 512ull);
		SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::Ready);
		SWIM_CHECK_EQUAL(capture.Invalidations.size(), iteration + 1u);
		SWIM_REQUIRE(arena->TryReset());
	}
	SWIM_CHECK_EQUAL(capture.MapCalls, 1u);
	SWIM_CHECK_EQUAL(capture.UnmapCalls, 0u);
	SWIM_CHECK_EQUAL(capture.IdleCalls, 0u);
	arena.reset();
	frames->Drain();
	SWIM_CHECK_EQUAL(capture.UnmapCalls, 1u);
	SWIM_CHECK_EQUAL(capture.CreateCalls, capture.DestroyCalls);
}

SWIM_TEST("RHI.Vulkan.ReadbackArena", "CoherentReadsSkipNativeInvalidation")
{
	Testing::VulkanMappedBufferCapture capture(true);
	auto buffer = capture.Device->CreateBuffer({ 64, Rhi::BufferUsage::TransferDestination,
		Rhi::MemoryPreference::GpuToCpu, {}, true });
	SWIM_REQUIRE(buffer);
	buffer->InvalidateMappedReads(0, 64);
	std::array<std::byte, 8> output{};
	buffer->Read(0, output);
	SWIM_CHECK(capture.Invalidations.empty());
	SWIM_CHECK_EQUAL(capture.MapCalls, 1u);
	SWIM_CHECK_EQUAL(capture.UnmapCalls, 0u);
}

SWIM_TEST("RHI.Vulkan.ReadbackArena", "ReadMappingDirectionAndRangesAreValidated")
{
	Testing::VulkanMappedBufferCapture capture;
	auto read = capture.Device->CreateBuffer({ 512, Rhi::BufferUsage::TransferDestination,
		Rhi::MemoryPreference::GpuToCpu, {}, true });
	auto write = capture.Device->CreateBuffer({ 512, Rhi::BufferUsage::TransferSource,
		Rhi::MemoryPreference::CpuToGpu, {}, true });
	auto unmapped = capture.Device->CreateBuffer({ 512, Rhi::BufferUsage::TransferDestination,
		Rhi::MemoryPreference::GpuToCpu, {} });
	SWIM_REQUIRE(read && write && unmapped);
	SWIM_CHECK(read->GetMappedWriteSpan().empty());
	SWIM_CHECK(write->GetMappedReadSpan().empty());
	SWIM_CHECK(unmapped->GetMappedReadSpan().empty());
	SWIM_CHECK_THROWS(write->InvalidateMappedReads(0, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(unmapped->InvalidateMappedReads(0, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(read->FlushMappedWrites(0, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(read->InvalidateMappedReads(513, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(read->InvalidateMappedReads(1, UINT64_MAX), std::invalid_argument);
	SWIM_CHECK_THROWS(read->InvalidateMappedReads(UINT64_MAX, 1), std::invalid_argument);
	read->InvalidateMappedReads(512, 0);
	SWIM_CHECK(capture.Invalidations.empty());
	read->InvalidateMappedReads(257, 1);
	SWIM_REQUIRE_EQUAL(capture.Invalidations.size(), 1u);
	SWIM_CHECK_EQUAL(capture.Invalidations[0].offset % 256, 0ull);
	SWIM_CHECK_EQUAL(capture.Invalidations[0].size, 256ull);
	std::array<std::byte, 4> output{};
	read->Read(0, output);
	SWIM_CHECK_EQUAL(capture.Invalidations.size(), 2u);
	SWIM_CHECK_EQUAL(capture.UnmapCalls, 0u);
}

SWIM_TEST("RHI.Vulkan.ReadbackArena", "SeparateReadbackAllocationsDoNotShareAtoms")
{
	Testing::VulkanMappedBufferCapture capture;
	auto first = capture.Device->CreateBuffer({ 31, Rhi::BufferUsage::TransferDestination,
		Rhi::MemoryPreference::GpuToCpu, {}, true });
	auto second = capture.Device->CreateBuffer({ 31, Rhi::BufferUsage::TransferDestination,
		Rhi::MemoryPreference::GpuToCpu, {}, true });
	SWIM_REQUIRE(first && second);
	first->InvalidateMappedReads(0, 31);
	second->InvalidateMappedReads(0, 31);
	SWIM_REQUIRE_EQUAL(capture.Invalidations.size(), 2u);
	const auto& a = capture.Invalidations[0];
	const auto& b = capture.Invalidations[1];
	SWIM_CHECK(a.memory != b.memory || a.offset + a.size <= b.offset || b.offset + b.size <= a.offset);
}

SWIM_TEST("RHI.Vulkan.ReadbackArena", "NativeFailureDoesNotExposeBytesAndLossStopsFurtherReads")
{
	Testing::VulkanMappedBufferCapture capture;
	Testing::MockDevice frameDevice;
	auto frames = Rhi::FrameContextRing::Create(frameDevice);
	auto arena = Rhi::ReadbackArena::Create(*capture.Device, { 64 });
	SWIM_REQUIRE(arena);
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	frames->BeginFrame();
	std::array batches{ arena.get() };
	frameDevice.LastTimeline->Complete(frames->SubmitCurrent(batches));
	capture.InvalidateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	std::array<std::byte, 4> output;
	output.fill(std::byte{ 91 });
	std::span<const std::byte> view = output;
	SWIM_CHECK_THROWS(arena->TryGetData(*slice, view), std::runtime_error);
	SWIM_CHECK(view.empty());
	SWIM_CHECK_THROWS(arena->TryRead(*slice, output), std::runtime_error);
	SWIM_CHECK_EQUAL(output[0], std::byte{ 91 });
	capture.InvalidateResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(arena->TryRead(*slice, output), Rhi::DeviceLostError);
	const auto invalidations = capture.Invalidations.size();
	view = output;
	SWIM_CHECK_THROWS(arena->TryGetData(*slice, view), Rhi::DeviceLostError);
	SWIM_CHECK(view.empty());
	SWIM_CHECK_THROWS(arena->TryReset(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.Invalidations.size(), invalidations);
	frames->Drain();
}

SWIM_TEST("RHI.Vulkan.ReadbackArena", "MapFailureCleansUpAndKnownLossPreventsCreation")
{
	Testing::VulkanMappedBufferCapture capture;
	capture.MapResult = VK_ERROR_MEMORY_MAP_FAILED;
	SWIM_CHECK(!Rhi::ReadbackArena::Create(*capture.Device, { 64 }));
	SWIM_CHECK_EQUAL(capture.CreateCalls, capture.DestroyCalls);
	VmaTotalStatistics stats{};
	vmaCalculateStatistics(capture.State->Allocator, &stats);
	SWIM_CHECK_EQUAL(stats.total.statistics.allocationCount, 0u);
	RhiVulkan::ObserveVulkanResult(*capture.State, VK_ERROR_DEVICE_LOST, "captured readback loss");
	const auto creates = capture.CreateCalls;
	SWIM_CHECK_THROWS(Rhi::ReadbackArena::Create(*capture.Device, { 64 }), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.CreateCalls, creates);
}
