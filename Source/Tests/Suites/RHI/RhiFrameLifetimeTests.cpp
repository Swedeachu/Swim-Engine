#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"

#include <cstdint>
#include <memory>

using namespace Swim::Testing;

namespace
{

	class LifetimeObject final : public Swim::Rhi::RhiObject
	{
	public:
		explicit LifetimeObject(std::uint32_t& destroyCount)
			: destroyCount(destroyCount)
		{
		}

		~LifetimeObject() override
		{
			++destroyCount;
		}

		std::uintptr_t GetNativeHandle() const override
		{
			return 6;
		}

	private:
		std::uint32_t& destroyCount;
	};

}

SWIM_TEST("RHI.FrameLifetime", "FrameContextsWaitOnlyWhenReused")
{
	MockDevice device;
	Swim::Rhi::FrameContextDesc desc;
	desc.FrameCount = 2;

	auto frames = Swim::Rhi::FrameContextRing::Create(device, desc);
	SWIM_CHECK(frames != nullptr);
	SWIM_CHECK_EQUAL(frames->BeginFrame().Index, std::uint32_t(0));
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(), std::uint64_t(1));
	SWIM_CHECK_EQUAL(frames->BeginFrame().Index, std::uint32_t(1));
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(), std::uint64_t(2));
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, std::uint32_t(0));

	SWIM_CHECK_EQUAL(frames->BeginFrame().Index, std::uint32_t(0));
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, std::uint32_t(1));
	SWIM_CHECK_EQUAL(device.WaitIdleCount, std::uint32_t(0));
}

SWIM_TEST("RHI.FrameLifetime", "FrameRetirementLivesUntilGpuCompletion")
{
	MockDevice device;
	Swim::Rhi::FrameContextDesc desc;
	desc.FrameCount = 2;
	auto frames = Swim::Rhi::FrameContextRing::Create(device, desc);
	std::uint32_t destroyCount = 0;

	frames->BeginFrame();
	frames->Retire(std::make_unique<LifetimeObject>(destroyCount));
	frames->SubmitCurrent();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(0));

	frames->BeginFrame();
	frames->SubmitCurrent();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(0));

	frames->BeginFrame();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(1));
}

SWIM_TEST("RHI.FrameLifetime", "ExplicitRetirementCollectsFromTimeline")
{
	MockDevice device;
	auto frames = Swim::Rhi::FrameContextRing::Create(device);
	std::uint32_t destroyCount = 0;

	for (std::uint64_t value = 1; value <= 5; ++value)
	{
		frames->BeginFrame();
		SWIM_CHECK_EQUAL(frames->SubmitCurrent(), value);
	}

	frames->RetireAt(5, std::make_unique<LifetimeObject>(destroyCount));
	frames->CollectCompleted();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(0));

	device.LastTimeline->Complete(4);
	frames->CollectCompleted();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(0));

	device.LastTimeline->Complete(5);
	frames->CollectCompleted();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(1));
}

SWIM_TEST("RHI.FrameLifetime", "SubmissionPreservesCallerTimelineSignals")
{
	MockDevice device;
	auto frames = Swim::Rhi::FrameContextRing::Create(device);
	MockTimeline externalTimeline;
	Swim::Rhi::TimelinePoint signal{ &externalTimeline, 7 };

	frames->BeginFrame();
	auto& commandList = frames->CreateCommandList();
	commandList.Begin();
	commandList.End();

	Swim::Rhi::CommandList* commandLists[] = { &commandList };
	Swim::Rhi::SubmitDesc submit;
	submit.CommandLists = commandLists;
	submit.SignalTimelines = std::span<const Swim::Rhi::TimelinePoint>(&signal, 1);
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(submit), std::uint64_t(1));

	SWIM_CHECK_EQUAL(device.queue.LastCommandListCount, std::uint32_t(1));
	SWIM_CHECK_EQUAL(device.queue.LastSignalTimelineCount, std::uint32_t(2));
	SWIM_CHECK_EQUAL(device.queue.LastFrameSignalValue, std::uint64_t(1));
	SWIM_CHECK_EQUAL(externalTimeline.GetCompletedValue(), std::uint64_t(7));
}

SWIM_TEST("RHI.FrameLifetime", "ZeroFrameRingIsRejected")
{
	MockDevice device;
	Swim::Rhi::FrameContextDesc desc;
	desc.FrameCount = 0;
	SWIM_CHECK(Swim::Rhi::FrameContextRing::Create(device, desc) == nullptr);
}

SWIM_TEST("RHI.FrameLifetime", "FutureRetirementRequiresScheduledTimelineValue")
{
	MockDevice device;
	auto frames = Swim::Rhi::FrameContextRing::Create(device);
	std::uint32_t destroyCount = 0;
	auto object = std::make_unique<LifetimeObject>(destroyCount);
	bool rejected = false;

	try
	{
		frames->RetireAt(1, std::move(object));
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}

	SWIM_CHECK(rejected);
	SWIM_CHECK(object != nullptr);
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(0));
	object.reset();
	SWIM_CHECK_EQUAL(destroyCount, std::uint32_t(1));
}

SWIM_TEST("RHI.FrameLifetime", "LastSubmittedPointTracksFrameTimeline")
{
	MockDevice device;
	auto frames = Swim::Rhi::FrameContextRing::Create(device);

	const Swim::Rhi::TimelinePoint initialPoint = frames->GetLastSubmittedPoint();
	SWIM_CHECK(initialPoint.Semaphore == &frames->GetTimeline());
	SWIM_CHECK_EQUAL(initialPoint.Value, std::uint64_t(0));

	frames->BeginFrame();
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(), std::uint64_t(1));

	const Swim::Rhi::TimelinePoint submittedPoint = frames->GetLastSubmittedPoint();
	SWIM_CHECK(submittedPoint.Semaphore == &frames->GetTimeline());
	SWIM_CHECK_EQUAL(submittedPoint.Value, std::uint64_t(1));
}

SWIM_TEST("RHI.FrameLifetime", "SkippedAcquisitionCancelsWithoutSubmissionOrTimelineAdvance")
{
	MockDevice device;
	auto frames = Swim::Rhi::FrameContextRing::Create(device, { Swim::Rhi::QueueType::Graphics, 2 });
	SWIM_REQUIRE(frames);
	frames->BeginFrame();
	frames->SubmitCurrent();
	const auto previousPoint = frames->GetLastSubmittedPoint();
	const auto skippedIndex = frames->BeginFrame().Index;
	for (unsigned retry = 0; retry < 4; ++retry)
	{
		frames->CancelFrame();
		SWIM_CHECK(frames->GetCurrentContext() == nullptr);
		SWIM_CHECK_EQUAL(frames->GetLastSubmittedValue(), previousPoint.Value);
		SWIM_CHECK_EQUAL(frames->BeginFrame().Index, skippedIndex);
	}
	SWIM_CHECK_EQUAL(frames->SubmitCurrent(), previousPoint.Value + 1);
}

SWIM_TEST("RHI.FrameLifetime", "CancelRejectsMissingOrRecordedFrames")
{
	MockDevice device;
	auto frames = Swim::Rhi::FrameContextRing::Create(device);
	SWIM_REQUIRE(frames);
	SWIM_CHECK_THROWS(frames->CancelFrame(), std::logic_error);
	frames->BeginFrame();
	auto& commands = frames->CreateCommandList();
	commands.Begin();
	commands.End();
	SWIM_CHECK_THROWS(frames->CancelFrame(), std::logic_error);
	SWIM_CHECK(frames->GetCurrentContext() != nullptr);
	frames->SubmitCurrent();
}

SWIM_TEST("RHI.FrameLifetime", "CancelDoesNotReleaseObjectsAwaitingFrameRetirement")
{
	MockDevice device;
	std::uint32_t destroyed = 0;
	auto frames = Swim::Rhi::FrameContextRing::Create(device);
	frames->BeginFrame();
	frames->Retire(std::make_unique<LifetimeObject>(destroyed));
	SWIM_CHECK_THROWS(frames->CancelFrame(), std::logic_error);
	SWIM_CHECK_EQUAL(destroyed, 0u);
	frames->SubmitCurrent();
	SWIM_CHECK_EQUAL(destroyed, 0u);
	frames->Drain();
	SWIM_CHECK_EQUAL(destroyed, 1u);
}
