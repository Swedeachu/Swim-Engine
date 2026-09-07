#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"

#include <array>

using namespace Swim;

SWIM_TEST("RHI.ReadbackFrame", "BatchResultsSurviveSlotReuseAndFrameRingDestruction")
{
	Testing::MockDevice device;
	auto arena = Rhi::ReadbackArena::Create(device, { 8 });
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	device.LastBuffer->Bytes[0] = std::byte{ 51 };
	{
		auto frames = Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 1 });
		frames->BeginFrame();
		std::array batches{ arena.get() };
		frames->SubmitCurrent(batches);
		frames->BeginFrame(); // Wait and reuse commands, but keep CPU results.
		SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 1u);
		frames->SubmitCurrent();
	}
	std::array<std::byte, 4> output{};
	SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::Ready);
	SWIM_CHECK_EQUAL(output[0], std::byte{ 51 });
	SWIM_CHECK(arena->TryReset());
}

SWIM_TEST("RHI.ReadbackFrame", "DestroyedArenaBufferRemainsOwnedUntilFrameCompletion")
{
	Testing::MockDevice device;
	unsigned destroyed = 0;
	auto frames = Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 1 });
	auto arena = Rhi::ReadbackArena::Create(device, { 8 });
	device.LastBuffer->OnDestroy = [&] { ++destroyed; };
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	frames->BeginFrame();
	std::array batches{ arena.get() };
	frames->SubmitCurrent(batches);
	arena.reset();
	SWIM_CHECK_EQUAL(destroyed, 0u);
	SWIM_CHECK_THROWS(slice->GetBuffer(), std::invalid_argument);
	device.LastTimeline->FailWait = true;
	SWIM_CHECK_THROWS(frames->BeginFrame(), std::runtime_error);
	SWIM_CHECK_EQUAL(destroyed, 0u);
	device.LastTimeline->FailWait = false;
	frames->BeginFrame();
	SWIM_CHECK_EQUAL(destroyed, 1u);
	frames->CancelFrame();
}

SWIM_TEST("RHI.ReadbackFrame", "FailedSubmitDoesNotSealBatchesOrAdvanceTimeline")
{
	Testing::MockDevice device;
	auto frames = Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 1, { 8 } });
	auto arena = Rhi::ReadbackArena::Create(device, { 8 });
	auto slice = arena->Allocate(4);
	SWIM_REQUIRE(slice);
	frames->BeginFrame();
	auto upload = frames->AllocateUpload(4);
	SWIM_REQUIRE(upload);
	auto* uploadBuffer = static_cast<Testing::MockMappedBuffer*>(upload->Resource);
	std::array batches{ arena.get() };
	uploadBuffer->FailFlush = true;
	SWIM_CHECK_THROWS(frames->SubmitCurrent(batches), std::runtime_error);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 0u);
	uploadBuffer->FailFlush = false;
	device.queue.FailSubmit = true;
	SWIM_CHECK_THROWS(frames->SubmitCurrent(batches), std::runtime_error);
	SWIM_CHECK_EQUAL(frames->GetLastSubmittedValue(), 0ull);
	std::array<std::byte, 4> output{};
	SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::NotSubmitted);
	SWIM_REQUIRE(arena->Allocate(4));
	device.queue.FailSubmit = false;
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(batches), 1ull);
	SWIM_CHECK_EQUAL(arena->TryRead(*slice, output), Rhi::ReadbackStatus::NotReady);
}

SWIM_TEST("RHI.ReadbackFrame", "InvalidBatchListsRejectEntireSubmissionBeforeQueueCall")
{
	Testing::MockDevice device;
	auto frames = Rhi::FrameContextRing::Create(device);
	auto arena = Rhi::ReadbackArena::Create(device, { 8 });
	auto empty = Rhi::ReadbackArena::Create(device, { 8 });
	SWIM_REQUIRE(arena->Allocate(4));
	frames->BeginFrame();
	std::array<Rhi::ReadbackArena*, 1> nulls{ nullptr };
	std::array duplicates{ arena.get(), arena.get() };
	std::array invalid{ arena.get(), empty.get() };
	SWIM_CHECK_THROWS(frames->SubmitCurrent(nulls), std::invalid_argument);
	SWIM_CHECK_THROWS(frames->SubmitCurrent(duplicates), std::invalid_argument);
	SWIM_CHECK_THROWS(frames->SubmitCurrent(invalid), std::logic_error);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 0u);
	std::array batches{ arena.get() };
	frames->SubmitCurrent(batches);
	frames->BeginFrame();
	SWIM_CHECK_THROWS(frames->SubmitCurrent(batches), std::logic_error);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 1u);
	frames->CancelFrame();
}

SWIM_TEST("RHI.ReadbackFrame", "ExplicitSubmitPreservesSignalsAndSealsMultipleBatchesTogether")
{
	Testing::MockDevice device;
	auto frames = Rhi::FrameContextRing::Create(device);
	auto first = Rhi::ReadbackArena::Create(device, { 8 });
	auto second = Rhi::ReadbackArena::Create(device, { 8 });
	auto a = first->Allocate(4);
	auto b = second->Allocate(4);
	SWIM_REQUIRE(a && b);
	Testing::MockTimeline external;
	Rhi::TimelinePoint point{ &external, 47 };
	Rhi::SubmitDesc desc;
	desc.SignalTimelines = { &point, 1 };
	std::array batches{ first.get(), second.get() };
	frames->BeginFrame();
	const auto value = frames->SubmitCurrent(desc, batches);
	SWIM_CHECK_EQUAL(device.queue.LastSignalTimelineCount, 2u);
	SWIM_CHECK_EQUAL(external.GetCompletedValue(), 47ull);
	SWIM_CHECK(!first->TryReset() && !second->TryReset());
	device.LastTimeline->Complete(value);
	std::array<std::byte, 4> output{};
	SWIM_CHECK_EQUAL(first->TryRead(*a, output), Rhi::ReadbackStatus::Ready);
	SWIM_CHECK_EQUAL(second->TryRead(*b, output), Rhi::ReadbackStatus::Ready);
	SWIM_CHECK(first->TryReset() && second->TryReset());
	frames->Drain();
}
