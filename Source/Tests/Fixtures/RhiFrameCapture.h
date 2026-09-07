#pragma once

#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"

#include <functional>

namespace Swim::Testing
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
			if (FailWait)
			{
				return false;
			}
			completedValue = value;
			return true;
		}

		void Complete(std::uint64_t value)
		{
			completedValue = value;
		}

		std::uint32_t WaitCount = 0;
		bool FailWait = false;

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
			if (BeforeSubmit)
			{
				BeforeSubmit();
			}
			if (FailSubmit)
			{
				throw std::runtime_error("Captured submission failure");
			}
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

		std::function<void()> BeforeSubmit;
		bool FailSubmit = false;
		MockTimeline* FrameTimeline = nullptr;
		std::uint32_t SubmitCount = 0;
		std::uint32_t WaitIdleCount = 0;
		std::uint32_t LastCommandListCount = 0;
		std::uint32_t LastSignalTimelineCount = 0;
		std::uint64_t LastFrameSignalValue = 0;
	};

	class MockMappedBuffer final : public Swim::Rhi::Buffer
	{
	public:
		explicit MockMappedBuffer(const Swim::Rhi::BufferDesc& desc)
			: desc(desc), Bytes(static_cast<std::size_t>(desc.Size))
		{
		}

		~MockMappedBuffer() override
		{
			if (OnDestroy)
			{
				OnDestroy();
			}
		}

		std::function<void()> OnDestroy;
		std::uintptr_t GetNativeHandle() const override { return 7; }
		const Swim::Rhi::BufferDesc& GetDesc() const override { return desc; }
		void Write(std::uint64_t, std::span<const std::byte>) override {}
		void Read(std::uint64_t, std::span<std::byte>) override {}
		std::span<std::byte> GetMappedWriteSpan() override
		{
			++MapAccessCount;
			return Bytes;
		}
		void FlushMappedWrites(std::uint64_t offset, std::uint64_t size) override
		{
			++FlushCount;
			if (FailFlush)
			{
				throw std::runtime_error("Captured flush failure");
			}
			FlushOffset = offset;
			FlushSize = size;
		}

		std::span<const std::byte> GetMappedReadSpan() override
		{
			return ExposeReadMapping ? std::span<const std::byte>(Bytes) : std::span<const std::byte>{};
		}
		void InvalidateMappedReads(std::uint64_t offset, std::uint64_t size) override
		{
			++InvalidateCount;
			if (FailInvalidate)
			{
				throw std::runtime_error("Captured invalidate failure");
			}
			InvalidateOffset = offset;
			InvalidateSize = size;
		}

		bool ExposeReadMapping = true;
		bool FailInvalidate = false;
		std::uint32_t InvalidateCount = 0;
		std::uint64_t InvalidateOffset = 0;
		std::uint64_t InvalidateSize = 0;
		Swim::Rhi::BufferDesc desc;
		std::vector<std::byte> Bytes;
		std::uint32_t MapAccessCount = 0;
		std::uint32_t FlushCount = 0;
		std::uint64_t FlushOffset = 0;
		std::uint64_t FlushSize = 0;
		bool FailFlush = false;
	};

	class MockDevice final : public Swim::Rhi::Device
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 5; }
		const Swim::Rhi::AdapterInfo& GetAdapterInfo() const override { return adapterInfo; }
		Swim::Rhi::Queue& GetQueue(Swim::Rhi::QueueType) override { return queue; }

		std::unique_ptr<Swim::Rhi::Swapchain> CreateSwapchain(Swim::Platform::Window&, const Swim::Rhi::SwapchainDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::Buffer> CreateBuffer(const Swim::Rhi::BufferDesc& desc) override
		{
			if (++BufferCreateCount == FailBufferCreate)
			{
				return nullptr;
			}
			auto buffer = std::make_unique<MockMappedBuffer>(desc);
			LastBuffer = buffer.get();
			buffer->ExposeReadMapping = ExposeReadMapping;
			return buffer;
		}
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

		bool ExposeReadMapping = true;
		MockMappedBuffer* LastBuffer = nullptr;
		std::uint32_t BufferCreateCount = 0;
		std::uint32_t FailBufferCreate = 0;
		Swim::Rhi::AdapterInfo adapterInfo{};
	};

} // namespace Swim::Testing
