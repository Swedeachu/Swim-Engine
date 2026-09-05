#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"

#include <cstdint>
#include <memory>

namespace
{

	class MockTimeline final : public Swim::Rhi::Timeline
	{
	public:
		std::uintptr_t GetNativeHandle() const override
		{
			return 1;
		}

		std::uint64_t GetCompletedValue() const override
		{
			return completedValue;
		}

		bool Wait(std::uint64_t value, std::uint64_t) override
		{
			++WaitCount;
			completedValue = value;
			return true;
		}

		void Complete(std::uint64_t value)
		{
			completedValue = value;
		}

		std::uint32_t WaitCount = 0;

	private:
		std::uint64_t completedValue = 0;
	};

	class MockCommandList final : public Swim::Rhi::CommandList
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 2; }
		void Begin() override { Recording = true; }
		void End() override { Recording = false; }
		void Transition(Swim::Rhi::Buffer&, Swim::Rhi::ResourceState, Swim::Rhi::ResourceState) override {}
		void Transition(Swim::Rhi::Texture&, Swim::Rhi::ResourceState, Swim::Rhi::ResourceState, const Swim::Rhi::TextureSubresourceRange&) override {}
		void CopyBuffer(Swim::Rhi::Buffer&, Swim::Rhi::Buffer&, const Swim::Rhi::BufferCopyRegion&) override {}
		void CopyTexture(Swim::Rhi::Texture&, Swim::Rhi::Texture&, const Swim::Rhi::TextureCopyRegion&) override {}
		void CopyBufferToTexture(Swim::Rhi::Buffer&, Swim::Rhi::Texture&, const Swim::Rhi::BufferTextureCopyRegion&) override {}
		void CopyTextureToBuffer(Swim::Rhi::Texture&, Swim::Rhi::Buffer&, const Swim::Rhi::BufferTextureCopyRegion&) override {}

		void BeginRendering(const Swim::Rhi::RenderingDesc&) override {}
		void EndRendering() override {}
		void BindGraphicsPipeline(Swim::Rhi::GraphicsPipeline&) override {}
		void BindComputePipeline(Swim::Rhi::ComputePipeline&) override {}
		void BindDescriptorTable(std::uint32_t, Swim::Rhi::DescriptorTable&) override {}
		void SetViewport(const Swim::Rhi::Viewport&) override {}
		void SetScissor(const Swim::Rhi::ScissorRect&) override {}
		void BindVertexBuffer(std::uint32_t, Swim::Rhi::Buffer&, std::uint64_t) override {}
		void BindIndexBuffer(Swim::Rhi::Buffer&, std::uint64_t, Swim::Rhi::IndexType) override {}
		void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
		void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
		void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
		void ResetQueries(Swim::Rhi::QueryPool&, std::uint32_t, std::uint32_t) override {}
		void WriteTimestamp(Swim::Rhi::QueryPool&, std::uint32_t, Swim::Rhi::TimestampStage) override {}

		bool Recording = false;
	};

	class MockCommandPool final : public Swim::Rhi::CommandPool
	{
	public:
		std::uintptr_t GetNativeHandle() const override
		{
			return 3;
		}

		std::unique_ptr<Swim::Rhi::CommandList> CreateCommandList() override
		{
			++CreateCount;
			return std::make_unique<MockCommandList>();
		}

		void Reset() override
		{
			++ResetCount;
		}

		std::uint32_t CreateCount = 0;
		std::uint32_t ResetCount = 0;
	};

	class MockQueue final : public Swim::Rhi::Queue
	{
	public:
		std::uintptr_t GetNativeHandle() const override
		{
			return 4;
		}

		Swim::Rhi::QueueType GetType() const override
		{
			return Swim::Rhi::QueueType::Graphics;
		}

		void Submit(const Swim::Rhi::SubmitDesc& desc) override
		{
			++SubmitCount;
			LastCommandListCount = static_cast<std::uint32_t>(desc.CommandLists.size());
			LastSignalTimelineCount = static_cast<std::uint32_t>(desc.SignalTimelines.size());
			for (const Swim::Rhi::TimelinePoint& point : desc.SignalTimelines)
			{
				auto* timeline = dynamic_cast<MockTimeline*>(point.Semaphore);
				if (timeline != nullptr && timeline != FrameTimeline)
				{
					timeline->Complete(point.Value);
				}
				if (timeline == FrameTimeline)
				{
					LastFrameSignalValue = point.Value;
				}
			}
		}

		void WaitIdle() override
		{
			++WaitIdleCount;
		}

		MockTimeline* FrameTimeline = nullptr;
		std::uint32_t SubmitCount = 0;
		std::uint32_t WaitIdleCount = 0;
		std::uint32_t LastCommandListCount = 0;
		std::uint32_t LastSignalTimelineCount = 0;
		std::uint64_t LastFrameSignalValue = 0;
	};

	class MockDevice final : public Swim::Rhi::Device
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 5; }
		const Swim::Rhi::AdapterInfo& GetAdapterInfo() const override { return adapterInfo; }
		Swim::Rhi::Queue& GetQueue(Swim::Rhi::QueueType) override { return queue; }

		std::unique_ptr<Swim::Rhi::Swapchain> CreateSwapchain(Swim::Platform::Window&, const Swim::Rhi::SwapchainDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::Buffer> CreateBuffer(const Swim::Rhi::BufferDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::Texture> CreateTexture(const Swim::Rhi::TextureDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::TextureView> CreateTextureView(Swim::Rhi::Texture&, const Swim::Rhi::TextureViewDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::Sampler> CreateSampler(const Swim::Rhi::SamplerDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::ShaderProgram> CreateShaderProgram(const Swim::Rhi::ShaderProgramDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::PipelineLayout> CreatePipelineLayout(const Swim::Rhi::PipelineLayoutDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::GraphicsPipeline> CreateGraphicsPipeline(const Swim::Rhi::GraphicsPipelineDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::ComputePipeline> CreateComputePipeline(const Swim::Rhi::ComputePipelineDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::DescriptorTable> CreateDescriptorTable(const Swim::Rhi::DescriptorTableDesc&) override { return nullptr; }

		std::unique_ptr<Swim::Rhi::CommandPool> CreateCommandPool(Swim::Rhi::QueueType) override
		{
			return std::make_unique<MockCommandPool>();
		}

		std::unique_ptr<Swim::Rhi::Semaphore> CreateGpuSemaphore() override { return nullptr; }
		std::unique_ptr<Swim::Rhi::Fence> CreateFence(bool) override { return nullptr; }

		std::unique_ptr<Swim::Rhi::Timeline> CreateTimeline(std::uint64_t initialValue) override
		{
			auto timeline = std::make_unique<MockTimeline>();
			timeline->Complete(initialValue);
			LastTimeline = timeline.get();
			queue.FrameTimeline = timeline.get();
			return timeline;
		}

		std::unique_ptr<Swim::Rhi::QueryPool> CreateQueryPool(const Swim::Rhi::QueryPoolDesc&) override { return nullptr; }
		void WaitIdle() override { ++WaitIdleCount; }

		MockQueue queue;
		MockTimeline* LastTimeline = nullptr;
		std::uint32_t WaitIdleCount = 0;

	private:
		Swim::Rhi::AdapterInfo adapterInfo{};
	};

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
