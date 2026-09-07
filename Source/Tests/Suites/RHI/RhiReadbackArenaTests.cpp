#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"

#include <array>

using namespace Swim;

SWIM_TEST("RHI.ReadbackArena", "AlignedBoundedSlicesRejectOverflowWithoutAdvancing")
{
	Testing::MockDevice device;
	auto arena = Rhi::ReadbackArena::Create(device, { 65 });
	SWIM_REQUIRE(arena);
	auto first = arena->Allocate(3, 1);
	auto second = arena->Allocate(5, 16);
	SWIM_REQUIRE(first && second);
	SWIM_CHECK(&first->GetBuffer() == &second->GetBuffer());
	SWIM_CHECK_EQUAL(first->GetOffset(), 0ull);
	SWIM_CHECK_EQUAL(second->GetOffset(), 16ull);
	SWIM_CHECK_EQUAL(second->GetSize(), 5ull);
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 21ull);
	SWIM_CHECK(!arena->Allocate(42));
	SWIM_CHECK(!arena->Allocate(UINT64_MAX));
	SWIM_CHECK(!arena->Allocate(1, 1ull << 63));
	SWIM_CHECK_THROWS(arena->Allocate(0), std::invalid_argument);
	SWIM_CHECK_THROWS(arena->Allocate(1, 3), std::invalid_argument);
	SWIM_CHECK_THROWS(arena->Allocate(1, 0), std::invalid_argument);
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 21ull);
	SWIM_REQUIRE(arena->Allocate(41));
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 65ull);
	SWIM_CHECK(!arena->Allocate(1));
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
	SWIM_CHECK_EQUAL(first->GetBuffer().GetDesc().Memory, Rhi::MemoryPreference::GpuToCpu);
	SWIM_CHECK(first->GetBuffer().GetDesc().PersistentMap);
}

SWIM_TEST("RHI.ReadbackArena", "CreationRequiresCapacityAndBackendReadMapping")
{
	Testing::MockDevice device;
	SWIM_CHECK(!Rhi::ReadbackArena::Create(device, {}));
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 0u);
	device.FailBufferCreate = 1;
	SWIM_CHECK(!Rhi::ReadbackArena::Create(device, { 32 }));
	device.ExposeReadMapping = false;
	SWIM_CHECK(!Rhi::ReadbackArena::Create(device, { 32 }));
}

SWIM_TEST("RHI.ReadbackArena", "PendingReadsDoNotExposeBytesOrInvalidate")
{
	Testing::MockDevice device;
	auto arena = Rhi::ReadbackArena::Create(device, { 16 });
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	auto* buffer = device.LastBuffer;
	std::array<std::byte, 4> output;
	output.fill(std::byte{ 99 });
	std::span<const std::byte> view = output;
	SWIM_CHECK_EQUAL(arena->TryGetData(*slice, view), Rhi::ReadbackStatus::NotSubmitted);
	SWIM_CHECK(view.empty());
	SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::NotSubmitted);
	auto frames = Rhi::FrameContextRing::Create(device);
	frames->BeginFrame();
	std::array batches{ arena.get() };
	frames->SubmitCurrent(batches);
	view = output;
	SWIM_CHECK_EQUAL(arena->TryGetData(*slice, view), Rhi::ReadbackStatus::NotReady);
	SWIM_CHECK(view.empty());
	SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::NotReady);
	SWIM_CHECK(std::all_of(output.begin(), output.end(), [](std::byte byte) { return byte == std::byte{ 99 }; }));
	SWIM_CHECK_EQUAL(buffer->InvalidateCount, 0u);
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 0u);
	SWIM_CHECK(!arena->TryReset());
	SWIM_CHECK_THROWS(arena->Allocate(4), std::logic_error);
}

SWIM_TEST("RHI.ReadbackArena", "CompletedBatchInvalidatesOnceAndCopiesExactSlices")
{
	Testing::MockDevice device;
	auto arena = Rhi::ReadbackArena::Create(device, { 32 });
	auto first = arena->Allocate(4);
	auto second = arena->Allocate(8, 16);
	SWIM_REQUIRE(first && second);
	auto* buffer = device.LastBuffer;
	std::fill(buffer->Bytes.begin(), buffer->Bytes.begin() + 4, std::byte{ 31 });
	std::fill(buffer->Bytes.begin() + 16, buffer->Bytes.begin() + 24, std::byte{ 47 });
	auto frames = Rhi::FrameContextRing::Create(device);
	frames->BeginFrame();
	std::array batches{ arena.get() };
	const auto signal = frames->SubmitCurrent(batches);
	device.LastTimeline->Complete(signal);
	std::array<std::byte, 8> output{};
	SWIM_CHECK_EQUAL(arena->TryRead(*second, output), Rhi::ReadbackStatus::Ready);
	SWIM_CHECK(std::all_of(output.begin(), output.end(), [](std::byte byte) { return byte == std::byte{ 47 }; }));
	std::span<const std::byte> view;
	SWIM_CHECK_EQUAL(arena->TryGetData(*first, view), Rhi::ReadbackStatus::Ready);
	SWIM_CHECK_EQUAL(view.size(), 4u);
	SWIM_CHECK_EQUAL(view[0], std::byte{ 31 });
	SWIM_CHECK_EQUAL(buffer->InvalidateCount, 1u);
	SWIM_CHECK_EQUAL(buffer->InvalidateOffset, 0ull);
	SWIM_CHECK_EQUAL(buffer->InvalidateSize, 24ull);
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 0u);
	SWIM_CHECK(arena->TryReset());
	auto reused = arena->Allocate(32);
	SWIM_REQUIRE(reused);
	SWIM_CHECK_EQUAL(reused->GetOffset(), 0ull);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
}

SWIM_TEST("RHI.ReadbackArena", "StaleForeignAndInvalidSlicesCannotReadNewBatch")
{
	Testing::MockDevice device;
	auto arena = Rhi::ReadbackArena::Create(device, { 8 });
	auto other = Rhi::ReadbackArena::Create(device, { 8 });
	auto slice = arena->Allocate(4);
	auto foreign = other->Allocate(4);
	SWIM_REQUIRE(slice && foreign);
	std::array<std::byte, 4> output{};
	std::span<const std::byte> view = output;
	SWIM_CHECK_THROWS(arena->TryGetData({}, view), std::invalid_argument);
	SWIM_CHECK(view.empty());
	SWIM_CHECK_THROWS(arena->TryRead(*foreign, output), std::invalid_argument);
	SWIM_CHECK_THROWS(arena->TryRead(*slice, std::span(output).first(3)), std::invalid_argument);
	SWIM_REQUIRE(arena->TryReset());
	SWIM_CHECK_THROWS(arena->TryRead(*slice, output), std::invalid_argument);
	SWIM_CHECK_THROWS(slice->GetBuffer(), std::invalid_argument);
	auto fresh = arena->Allocate(4);
	SWIM_REQUIRE(fresh);
	arena.reset();
	SWIM_CHECK_THROWS(fresh->GetBuffer(), std::invalid_argument);
}

SWIM_TEST("RHI.ReadbackArena", "InvalidationFailurePreservesOutputAndAllowsRetry")
{
	Testing::MockDevice device;
	auto arena = Rhi::ReadbackArena::Create(device, { 8 });
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	auto* buffer = device.LastBuffer;
	auto frames = Rhi::FrameContextRing::Create(device);
	frames->BeginFrame();
	std::array batches{ arena.get() };
	device.LastTimeline->Complete(frames->SubmitCurrent(batches));
	buffer->FailInvalidate = true;
	std::array<std::byte, 4> output;
	output.fill(std::byte{ 99 });
	std::span<const std::byte> view = output;
	SWIM_CHECK_THROWS(arena->TryGetData(*slice, view), std::runtime_error);
	SWIM_CHECK(view.empty());
	SWIM_CHECK_THROWS(arena->TryRead(*slice, output), std::runtime_error);
	SWIM_CHECK_EQUAL(output[0], std::byte{ 99 });
	buffer->FailInvalidate = false;
	buffer->Bytes[0] = std::byte{ 17 };
	SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::Ready);
	SWIM_CHECK_EQUAL(output[0], std::byte{ 17 });
	SWIM_CHECK_EQUAL(buffer->InvalidateCount, 3u);
}
