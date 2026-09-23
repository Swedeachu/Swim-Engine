#pragma once

#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"

#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"

#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

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

	// Captured command stream shared by every list a MockDevice creates.
	struct MockCommand
	{
		std::string Kind;
		const void* Source = nullptr;
		const void* Destination = nullptr;
		std::uint64_t SourceOffset = 0;
		std::uint64_t DestinationOffset = 0;
		std::uint64_t Size = 0;
		Swim::Rhi::ResourceState Before = Swim::Rhi::ResourceState::Undefined;
		Swim::Rhi::ResourceState After = Swim::Rhi::ResourceState::Undefined;
		std::vector<std::byte> Data; // PushConstants bytes.
	};

	// Host-memory texture: one tightly packed byte vector per mip/layer.
	class MockTexture final : public Swim::Rhi::Texture
	{
	public:
		explicit MockTexture(const Swim::Rhi::TextureDesc& desc) : desc(desc)
		{
			this->desc.DebugName = {};
			const std::uint32_t texel = Swim::Rhi::GetUncompressedColorTexelBytes(desc.PixelFormat);
			for (std::uint32_t layer = 0; layer < desc.ArrayLayers; ++layer)
			{
				for (std::uint32_t mip = 0; mip < desc.MipLevels; ++mip)
				{
					const auto e = MipExtent(mip);
					Subresources.emplace_back(std::size_t(texel) * e.Width * e.Height * e.Depth);
				}
			}
		}

		std::uintptr_t GetNativeHandle() const override { return 8; }
		const Swim::Rhi::TextureDesc& GetDesc() const override { return desc; }

		Swim::Rhi::Extent3D MipExtent(std::uint32_t mip) const
		{
			const auto shrink = [&](std::uint32_t v) { return std::max(1u, v >> mip); };
			return { shrink(desc.Extent.Width), shrink(desc.Extent.Height), shrink(desc.Extent.Depth) };
		}

		std::vector<std::byte>& Bytes(const Swim::Rhi::TextureSubresource& sub)
		{
			return Subresources[std::size_t(sub.ArrayLayer) * desc.MipLevels + sub.MipLevel];
		}

		// Copies a tightly packed region between `buffer` and this subresource.
		void CopyRegion(std::span<std::byte> buffer, const Swim::Rhi::BufferTextureCopyRegion& region, bool toTexture)
		{
			const std::size_t texel = Swim::Rhi::GetUncompressedColorTexelBytes(desc.PixelFormat);
			auto& image = Bytes(region.Subresource);
			const auto e = MipExtent(region.Subresource.MipLevel);
			std::size_t cursor = static_cast<std::size_t>(region.BufferOffset);
			for (std::uint32_t z = 0; z < region.Extent.Depth; ++z)
			{
				for (std::uint32_t y = 0; y < region.Extent.Height; ++y)
				{
					const std::size_t row = (((std::size_t(region.TextureOffset.Z) + z) * e.Height + region.TextureOffset.Y + y) * e.Width +
						region.TextureOffset.X) * texel;
					const std::size_t bytes = std::size_t(region.Extent.Width) * texel;
					if (toTexture)
					{
						std::memcpy(image.data() + row, buffer.data() + cursor, bytes);
					}
					else
					{
						std::memcpy(buffer.data() + cursor, image.data() + row, bytes);
					}
					cursor += bytes;
				}
			}
		}

		std::vector<std::vector<std::byte>> Subresources;

	private:
		Swim::Rhi::TextureDesc desc;
	};

	class MockTextureView final : public Swim::Rhi::TextureView
	{
	public:
		MockTextureView(Swim::Rhi::Texture& texture, const Swim::Rhi::TextureViewDesc& desc) : texture(texture), desc(desc)
		{
			this->desc.DebugName = {};
		}

		std::uintptr_t GetNativeHandle() const override { return 9; }
		Swim::Rhi::Texture& GetTexture() const override { return texture; }
		const Swim::Rhi::TextureViewDesc& GetDesc() const override { return desc; }

	private:
		Swim::Rhi::Texture& texture;
		Swim::Rhi::TextureViewDesc desc;
	};

	class MockSampler final : public Swim::Rhi::Sampler
	{
	public:
		explicit MockSampler(const Swim::Rhi::SamplerDesc& desc) : desc(desc) { this->desc.DebugName = {}; }
		std::uintptr_t GetNativeHandle() const override { return 10; }
		const Swim::Rhi::SamplerDesc& GetDesc() const override { return desc; }

	private:
		Swim::Rhi::SamplerDesc desc;
	};

	class MockShaderProgram final : public Swim::Rhi::ShaderProgram
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 11; }
		const Swim::Rhi::ShaderProgramInterface& GetInterface() const override { return Interface; }
		Swim::Rhi::ShaderProgramInterface Interface;
	};

	class MockComputePipeline final : public Swim::Rhi::ComputePipeline
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 14; }
	};

	// A layout whose (merged) interface is set directly by the test.
	class MockPipelineLayout final : public Swim::Rhi::PipelineLayout
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 12; }
		Swim::Rhi::ShaderProgram& GetProgram() const override { return program; }
		const Swim::Rhi::ShaderProgramInterface& GetInterface() const override { return program.Interface; }
		mutable MockShaderProgram program;
	};

	// Records the resource currently written to each (binding, element).
	class MockDescriptorTable final : public Swim::Rhi::DescriptorTable
	{
	public:
		MockDescriptorTable(Swim::Rhi::PipelineLayout& layout, std::uint32_t space) : layout(layout), space(space) {}
		std::uintptr_t GetNativeHandle() const override { return 13; }
		Swim::Rhi::PipelineLayout& GetLayout() const override { return layout; }
		std::uint32_t GetSpace() const override { return space; }

		void Write(std::span<const Swim::Rhi::DescriptorWrite> writes) override
		{
			const Swim::Rhi::DescriptorSchemaDesc* schema = nullptr;
			for (const auto& candidate : layout.GetInterface().DescriptorSchemas)
			{
				schema = candidate.Space == space ? &candidate : schema;
			}
			for (const auto& write : writes)
			{
				const Swim::Rhi::DescriptorBindingDesc* binding = nullptr;
				for (const auto& candidate : schema->Bindings)
				{
					binding = candidate.Binding == write.Binding ? &candidate : binding;
				}
				if (!binding || write.ArrayIndex >= binding->Count || (Reject && (write.TextureResource == Reject || write.SamplerResource == Reject)))
				{
					throw std::invalid_argument("MockDescriptorTable rejected a write");
				}
			}
			for (const auto& write : writes)
			{
				Elements[{ write.Binding, write.ArrayIndex }] = write.TextureResource ? static_cast<const void*>(write.TextureResource)
					: write.SamplerResource ? static_cast<const void*>(write.SamplerResource)
											: static_cast<const void*>(write.BufferResource);
				++ElementWrites;
			}
		}

		const void* Element(std::uint32_t binding, std::uint32_t index) const
		{
			const auto found = Elements.find({ binding, index });
			return found == Elements.end() ? nullptr : found->second;
		}

		std::map<std::pair<std::uint32_t, std::uint32_t>, const void*> Elements;
		std::uint32_t ElementWrites = 0;
		const void* Reject = nullptr; // Writes of this resource throw, as an RHI validation failure would.

	private:
		Swim::Rhi::PipelineLayout& layout;
		std::uint32_t space;
	};

	class MockCommandList final : public Swim::Rhi::CommandList
	{
	public:
		std::uintptr_t GetNativeHandle() const override { return 2; }
		void Begin() override { Recording = true; }
		void End() override { Recording = false; }
		void Transition(Swim::Rhi::Buffer& buffer, Swim::Rhi::ResourceState before, Swim::Rhi::ResourceState after) override
		{
			Capture({ "TransitionBuffer", nullptr, &buffer, 0, 0, 0, before, after });
		}
		void Transition(Swim::Rhi::Texture& texture, Swim::Rhi::ResourceState before, Swim::Rhi::ResourceState after,
			const Swim::Rhi::TextureSubresourceRange&) override
		{
			Capture({ "TransitionTexture", nullptr, &texture, 0, 0, 0, before, after });
		}
		// Copies execute at record time on host-backed mocks. Upload writers run
		// before recording, so recorded order equals GPU order for these tests.
		void CopyBuffer(Swim::Rhi::Buffer& source, Swim::Rhi::Buffer& destination, const Swim::Rhi::BufferCopyRegion& region) override;
		void CopyTexture(Swim::Rhi::Texture&, Swim::Rhi::Texture&, const Swim::Rhi::TextureCopyRegion&) override {}
		void CopyBufferToTexture(Swim::Rhi::Buffer& source, Swim::Rhi::Texture& destination, const Swim::Rhi::BufferTextureCopyRegion& region) override;
		void CopyTextureToBuffer(Swim::Rhi::Texture& source, Swim::Rhi::Buffer& destination, const Swim::Rhi::BufferTextureCopyRegion& region) override;

		void BeginRendering(const Swim::Rhi::RenderingDesc&) override {}
		void EndRendering() override {}
		void BindGraphicsPipeline(Swim::Rhi::GraphicsPipeline&) override {}
		void BindComputePipeline(Swim::Rhi::ComputePipeline&) override {}
		void BindDescriptorTable(std::uint32_t, Swim::Rhi::DescriptorTable&) override {}
		void PushConstants(Swim::Rhi::ShaderStageMask, std::uint32_t offset, std::span<const std::byte> data) override
		{
			MockCommand command{ "PushConstants", nullptr, nullptr, offset, 0, data.size() };
			command.Data.assign(data.begin(), data.end());
			Capture(std::move(command));
		}
		void SetViewport(const Swim::Rhi::Viewport&) override {}
		void SetScissor(const Swim::Rhi::ScissorRect&) override {}
		void BindVertexBuffer(std::uint32_t, Swim::Rhi::Buffer&, std::uint64_t) override {}
		void BindIndexBuffer(Swim::Rhi::Buffer&, std::uint64_t, Swim::Rhi::IndexType) override {}
		void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
		void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
		void DrawIndexedIndirect(Swim::Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t) override
		{
			Capture({ "DrawIndexedIndirect", &arguments, nullptr, offset, 0, drawCount });
		}
		void DrawIndexedIndirectCount(Swim::Rhi::Buffer& arguments, std::uint64_t offset, Swim::Rhi::Buffer& count, std::uint64_t countOffset,
			std::uint32_t maxDrawCount, std::uint32_t) override
		{
			Capture({ "DrawIndexedIndirectCount", &arguments, &count, offset, countOffset, maxDrawCount });
		}
		void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override { Capture({ "Dispatch", nullptr, nullptr, x, y, z }); }
		void ResetQueries(Swim::Rhi::QueryPool&, std::uint32_t, std::uint32_t) override {}
		void WriteTimestamp(Swim::Rhi::QueryPool&, std::uint32_t, Swim::Rhi::TimestampStage) override {}

		bool Recording = false;
		std::shared_ptr<std::vector<MockCommand>> Log;

	private:
		void Capture(MockCommand command)
		{
			if (Log)
			{
				Log->push_back(std::move(command));
			}
		}
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
			auto list = std::make_unique<MockCommandList>();
			list->Log = Log;
			return list;
		}

		void Reset() override
		{
			++ResetCount;
		}

		std::uint32_t CreateCount = 0;
		std::uint32_t ResetCount = 0;
		std::shared_ptr<std::vector<MockCommand>> Log;
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
			: debugName(desc.DebugName), desc(desc), Bytes(static_cast<std::size_t>(desc.Size))
		{
			this->desc.DebugName = debugName; // Own the name, as the Vulkan backend does.
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
		std::string debugName;
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
		std::unique_ptr<Swim::Rhi::Texture> CreateTexture(const Swim::Rhi::TextureDesc& desc) override
		{
			if (!CreateTextures || ++TextureAttemptCount == FailTextureCreate)
			{
				return nullptr;
			}
			++TextureCreateCount;
			return std::make_unique<MockTexture>(desc);
		}
		std::unique_ptr<Swim::Rhi::TextureView> CreateTextureView(Swim::Rhi::Texture& texture, const Swim::Rhi::TextureViewDesc& desc) override
		{
			return CreateTextures ? std::make_unique<MockTextureView>(texture, desc) : nullptr;
		}
		std::unique_ptr<Swim::Rhi::Sampler> CreateSampler(const Swim::Rhi::SamplerDesc& desc) override
		{
			++SamplerCreateCount;
			return std::make_unique<MockSampler>(desc);
		}
		std::unique_ptr<Swim::Rhi::ShaderProgram> CreateShaderProgram(const Swim::Rhi::ShaderProgramDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::PipelineLayout> CreatePipelineLayout(const Swim::Rhi::PipelineLayoutDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::GraphicsPipeline> CreateGraphicsPipeline(const Swim::Rhi::GraphicsPipelineDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::ComputePipeline> CreateComputePipeline(const Swim::Rhi::ComputePipelineDesc&) override { return nullptr; }
		std::unique_ptr<Swim::Rhi::DescriptorTable> CreateDescriptorTable(const Swim::Rhi::DescriptorTableDesc& desc) override
		{
			if (!desc.Layout || FailDescriptorTable)
			{
				return nullptr;
			}
			auto table = std::make_unique<MockDescriptorTable>(*desc.Layout, desc.Space);
			LastDescriptorTable = table.get();
			return table;
		}

		std::unique_ptr<Swim::Rhi::CommandPool> CreateCommandPool(Swim::Rhi::QueueType) override
		{
			auto pool = std::make_unique<MockCommandPool>();
			pool->Log = Commands;
			return pool;
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
		bool CreateTextures = false;
		std::uint32_t TextureCreateCount = 0;
		std::uint32_t TextureAttemptCount = 0;
		std::uint32_t FailTextureCreate = 0;
		std::uint32_t SamplerCreateCount = 0;
		bool FailDescriptorTable = false;
		MockDescriptorTable* LastDescriptorTable = nullptr;
		std::shared_ptr<std::vector<MockCommand>> Commands = std::make_shared<std::vector<MockCommand>>();
	};

	inline void MockCommandList::CopyBuffer(
		Swim::Rhi::Buffer& source, Swim::Rhi::Buffer& destination, const Swim::Rhi::BufferCopyRegion& region)
	{
		Capture({ "CopyBuffer", &source, &destination, region.SourceOffset, region.DestinationOffset, region.Size });
		auto* from = dynamic_cast<MockMappedBuffer*>(&source);
		auto* to = dynamic_cast<MockMappedBuffer*>(&destination);
		if (from && to && region.SourceOffset + region.Size <= from->Bytes.size() &&
			region.DestinationOffset + region.Size <= to->Bytes.size())
		{
			std::memmove(to->Bytes.data() + region.DestinationOffset, from->Bytes.data() + region.SourceOffset,
				static_cast<std::size_t>(region.Size));
		}
	}

	inline void MockCommandList::CopyBufferToTexture(
		Swim::Rhi::Buffer& source, Swim::Rhi::Texture& destination, const Swim::Rhi::BufferTextureCopyRegion& region)
	{
		Capture({ "CopyBufferToTexture", &source, &destination, region.BufferOffset, 0, 0 });
		auto* from = dynamic_cast<MockMappedBuffer*>(&source);
		auto* to = dynamic_cast<MockTexture*>(&destination);
		if (from && to)
		{
			to->CopyRegion(from->Bytes, region, true);
		}
	}

	inline void MockCommandList::CopyTextureToBuffer(
		Swim::Rhi::Texture& source, Swim::Rhi::Buffer& destination, const Swim::Rhi::BufferTextureCopyRegion& region)
	{
		Capture({ "CopyTextureToBuffer", &source, &destination, 0, region.BufferOffset, 0 });
		auto* from = dynamic_cast<MockTexture*>(&source);
		auto* to = dynamic_cast<MockMappedBuffer*>(&destination);
		if (from && to)
		{
			from->CopyRegion(to->Bytes, region, false);
		}
	}

} // namespace Swim::Testing
