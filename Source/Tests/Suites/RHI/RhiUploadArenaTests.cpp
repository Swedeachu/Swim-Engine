#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"

#include <array>

using namespace Swim;

SWIM_TEST("RHI.UploadArena", "AlignedSlicesShareOneMappedBufferAndPreserveBytes")
{
	Testing::MockDevice device;
	auto arena = Rhi::UploadArena::Create(device, { 64 });
	SWIM_REQUIRE(arena);
	const std::array data{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
	auto first = arena->Write(data);
	auto second = arena->Allocate(5, 16);
	SWIM_REQUIRE(first && second);
	SWIM_CHECK(first->Resource == second->Resource);
	SWIM_CHECK_EQUAL(first->Offset, 0ull);
	SWIM_CHECK_EQUAL(second->Offset, 16ull);
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 21ull);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
	SWIM_CHECK(first->Resource->GetDesc().PersistentMap);
	SWIM_CHECK_EQUAL(first->Resource->GetDesc().Memory, Rhi::MemoryPreference::CpuToGpu);
	SWIM_CHECK(std::equal(data.begin(), data.end(), first->Bytes.begin()));
	std::fill(second->Bytes.begin(), second->Bytes.end(), std::byte{ 9 });
	arena->Flush();
	SWIM_CHECK_EQUAL(device.LastBuffer->FlushOffset, 0ull);
	SWIM_CHECK_EQUAL(device.LastBuffer->FlushSize, 21ull);
	arena->Flush();
	SWIM_CHECK_EQUAL(device.LastBuffer->FlushCount, 2u);
	arena->Reset();
	const auto reused = arena->Allocate(64);
	SWIM_REQUIRE(reused);
	SWIM_CHECK_EQUAL(reused->Offset, 0ull);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
}

SWIM_TEST("RHI.UploadArena", "ExhaustionAndInvalidRequestsDoNotAdvanceCursor")
{
	Testing::MockDevice device;
	auto arena = Rhi::UploadArena::Create(device, { 33 });
	SWIM_REQUIRE(arena->Allocate(1));
	SWIM_CHECK(!arena->Allocate(32));
	SWIM_CHECK(!arena->Allocate(UINT64_MAX));
	SWIM_CHECK(!arena->Allocate(1, 1ull << 63));
	SWIM_CHECK_THROWS(arena->Allocate(0), std::invalid_argument);
	SWIM_CHECK_THROWS(arena->Allocate(1, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(arena->Allocate(1, 3), std::invalid_argument);
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 1ull);
	SWIM_REQUIRE(arena->Allocate(29));
	SWIM_CHECK_EQUAL(arena->GetUsedBytes(), 33ull);
	SWIM_CHECK(!arena->Allocate(1));
}

SWIM_TEST("RHI.UploadArena", "DescriptorUsageHonorsAdapterOffsetAlignment")
{
	Testing::MockDevice device;
	device.adapterInfo.Capabilities.MinUniformBufferOffsetAlignment = 256;
	device.adapterInfo.Capabilities.MinStorageBufferOffsetAlignment = 64;
	auto arena = Rhi::UploadArena::Create(device, { 1024,
		Rhi::BufferUsage::Uniform | Rhi::BufferUsage::Storage });
	SWIM_REQUIRE(arena);
	const auto first = arena->Allocate(5, 1);
	const auto second = arena->Allocate(5, 1);
	const auto third = arena->Allocate(5, 512);
	SWIM_REQUIRE(first && second && third);
	SWIM_CHECK_EQUAL(first->Offset, 0ull);
	SWIM_CHECK_EQUAL(second->Offset, 256ull);
	SWIM_CHECK_EQUAL(third->Offset, 512ull);
}

SWIM_TEST("RHI.UploadArena", "InvalidConfigurationAndAllocationFailureAreRejected")
{
	Testing::MockDevice device;
	SWIM_CHECK(!Rhi::UploadArena::Create(device, {}));
	SWIM_CHECK(!Rhi::UploadArena::Create(device, { 64, Rhi::BufferUsage::None }));
	SWIM_CHECK(!Rhi::UploadArena::Create(device, { 64, Rhi::BufferUsage::TransferDestination }));
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 0u);
	device.FailBufferCreate = 1;
	SWIM_CHECK(!Rhi::UploadArena::Create(device, { 64 }));
	device.adapterInfo.Capabilities.MinUniformBufferOffsetAlignment = 6;
	SWIM_CHECK(!Rhi::UploadArena::Create(device, { 64, Rhi::BufferUsage::Uniform }));
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
}

SWIM_TEST("RHI.UploadArena", "FrameSlotsFlushBeforeSubmitAndWaitBeforeReuse")
{
	Testing::MockDevice device;
	auto frames = Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 2, { 32 } });
	SWIM_REQUIRE(frames);
	SWIM_CHECK_THROWS(frames->AllocateUpload(1), std::logic_error);
	frames->BeginFrame();
	auto first = frames->AllocateUpload(32);
	SWIM_REQUIRE(first);
	first->Bytes[0] = std::byte{ 47 };
	auto* buffer = static_cast<Testing::MockUploadBuffer*>(first->Resource);
	device.queue.BeforeSubmit = [&]
	{
		SWIM_CHECK_EQUAL(buffer->FlushCount, 1u);
		SWIM_CHECK_EQUAL(buffer->FlushSize, 32ull);
	};
	frames->SubmitCurrent();
	device.queue.BeforeSubmit = {};
	SWIM_CHECK_THROWS(frames->AllocateUpload(1), std::logic_error);
	frames->BeginFrame();
	auto second = frames->AllocateUpload(32);
	SWIM_REQUIRE(second);
	SWIM_CHECK(first->Resource != second->Resource);
	second->Bytes[0] = std::byte{ 81 };
	SWIM_CHECK_EQUAL(buffer->Bytes[0], std::byte{ 47 });
	frames->SubmitCurrent();
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 0u);
	device.LastTimeline->FailWait = true;
	SWIM_CHECK_THROWS(frames->BeginFrame(), std::runtime_error);
	SWIM_CHECK(frames->GetCurrentContext() == nullptr);
	SWIM_CHECK_EQUAL(buffer->Bytes[0], std::byte{ 47 });
	device.LastTimeline->FailWait = false;
	frames->BeginFrame();
	auto reused = frames->AllocateUpload(32);
	SWIM_REQUIRE(reused);
	SWIM_CHECK(reused->Resource == first->Resource);
	SWIM_CHECK_EQUAL(reused->Offset, 0ull);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 2u);
	SWIM_CHECK_EQUAL(device.queue.WaitIdleCount, 0u);
	frames->Drain();
}

SWIM_TEST("RHI.UploadArena", "FailedFlushOrSubmitPreservesActiveFrameForRetry")
{
	Testing::MockDevice device;
	auto frames = Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 1, { 8 } });
	frames->BeginFrame();
	SWIM_REQUIRE(frames->AllocateUpload(8));
	device.LastBuffer->FailFlush = true;
	SWIM_CHECK_THROWS(frames->SubmitCurrent(), std::runtime_error);
	SWIM_CHECK(frames->GetCurrentContext() != nullptr);
	SWIM_CHECK_EQUAL(frames->GetLastSubmittedValue(), 0ull);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 0u);
	SWIM_CHECK(!frames->AllocateUpload(1));
	device.LastBuffer->FailFlush = false;
	device.queue.FailSubmit = true;
	SWIM_CHECK_THROWS(frames->SubmitCurrent(), std::runtime_error);
	SWIM_CHECK_EQUAL(frames->GetLastSubmittedValue(), 0ull);
	SWIM_CHECK(frames->GetCurrentContext() != nullptr);
	device.queue.FailSubmit = false;
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(), 1ull);
	SWIM_CHECK_EQUAL(device.LastBuffer->FlushCount, 3u);
}

SWIM_TEST("RHI.UploadArena", "CancellationDisabledStorageAndDrainRespectSliceLifetime")
{
	Testing::MockDevice device;
	auto disabled = Rhi::FrameContextRing::Create(device);
	disabled->BeginFrame();
	SWIM_CHECK_THROWS(disabled->AllocateUpload(1), std::logic_error);
	disabled->CancelFrame();
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 0u);
	auto frames = Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 1, { 8 } });
	frames->BeginFrame();
	frames->CancelFrame();
	frames->BeginFrame();
	const std::array data{ std::byte{ 12 } };
	SWIM_REQUIRE(frames->WriteUpload(data));
	SWIM_CHECK_THROWS(frames->CancelFrame(), std::logic_error);
	frames->SubmitCurrent();
	frames->Drain();
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 1u);
	frames->BeginFrame();
	const auto reused = frames->AllocateUpload(8);
	SWIM_REQUIRE(reused);
	SWIM_CHECK_EQUAL(reused->Offset, 0ull);
	frames->Drain();
	frames->BeginFrame();
	frames->CancelFrame();
}

SWIM_TEST("RHI.UploadArena", "PartialFrameStorageCreationFailsCleanly")
{
	Testing::MockDevice device;
	device.FailBufferCreate = 2;
	SWIM_CHECK(!Rhi::FrameContextRing::Create(device, { Rhi::QueueType::Graphics, 3, { 64 } }));
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 2u);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 0u);
}
