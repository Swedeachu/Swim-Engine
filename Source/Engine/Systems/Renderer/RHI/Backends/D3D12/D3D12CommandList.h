#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"
#include "Engine/Systems/Renderer/RHI/RhiDescriptorBindings.h"

#include <vector>

namespace Swim::RhiD3D12
{

	class D3D12PipelineLayout;
	class D3D12QueryPool;

	D3D12_COMMAND_LIST_TYPE ToCommandListType(Rhi::QueueType type);

	// A pool's command allocators. Every list holds one; a destroyed list hands it back for
	// the next list, and a pool reset resets them all. Copy staging blocks follow the same
	// lifetime: a list retires its blocks, and a pool reset (its GPU work done) frees them.
	struct D3D12AllocatorSet
	{
		std::mutex Mutex;
		std::vector<ComPtr<ID3D12CommandAllocator>> All;
		std::vector<ID3D12CommandAllocator*> Free;
		std::vector<std::unique_ptr<D3D12Buffer>> RetiredStaging;
		std::vector<std::unique_ptr<D3D12Buffer>> FreeStaging;
		// Closed native lists of the pool's type, kept for the next RHI list: Reset may be
		// called on a closed list while it still executes (ID3D12GraphicsCommandList::Reset),
		// so a list is reused at once instead of created per frame.
		std::vector<ComPtr<ID3D12GraphicsCommandList>> FreeLists;
	};

	class D3D12CommandList final : public Rhi::CommandList
	{

	  public:
		D3D12CommandList(std::shared_ptr<D3D12DeviceState> state, Rhi::QueueType type, std::shared_ptr<D3D12AllocatorSet> allocators);
		~D3D12CommandList() override;

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(list.Get()); }

		void Begin() override;
		void End() override;
		void BeginDebugLabel(std::string_view name, const std::array<float, 4>& color) override;
		void EndDebugLabel() override;
		void InsertDebugLabel(std::string_view name, const std::array<float, 4>& color) override;

		void Transition(Rhi::Buffer& buffer, Rhi::ResourceState before, Rhi::ResourceState after) override;
		void Transition(
			Rhi::Texture& texture, Rhi::ResourceState before, Rhi::ResourceState after, const Rhi::TextureSubresourceRange& range) override;
		void Transitions(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures) override;

		void CopyBuffer(Rhi::Buffer& source, Rhi::Buffer& destination, const Rhi::BufferCopyRegion& region) override;
		void CopyTexture(Rhi::Texture& source, Rhi::Texture& destination, const Rhi::TextureCopyRegion& region) override;
		void CopyBufferToTexture(Rhi::Buffer& source, Rhi::Texture& destination, const Rhi::BufferTextureCopyRegion& region) override;
		void CopyTextureToBuffer(Rhi::Texture& source, Rhi::Buffer& destination, const Rhi::BufferTextureCopyRegion& region) override;

		void BeginRendering(const Rhi::RenderingDesc& desc) override;
		void EndRendering() override;

		void BindGraphicsPipeline(Rhi::GraphicsPipeline& pipeline) override;
		void BindComputePipeline(Rhi::ComputePipeline& pipeline) override;
		void BindDescriptorTable(std::uint32_t space, Rhi::DescriptorTable& table) override;
		void PushConstants(Rhi::ShaderStageMask stages, std::uint32_t offset, std::span<const std::byte> data) override;
		void SetViewport(const Rhi::Viewport& viewport) override;
		void SetScissor(const Rhi::ScissorRect& scissor) override;
		void BindVertexBuffer(std::uint32_t slot, Rhi::Buffer& buffer, std::uint64_t offset) override;
		void BindIndexBuffer(Rhi::Buffer& buffer, std::uint64_t offset, Rhi::IndexType type) override;

		void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) override;
		void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance) override;
		void DrawIndexedIndirect(Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) override;
		void DrawIndexedIndirectCount(Rhi::Buffer& arguments, std::uint64_t offset, Rhi::Buffer& count, std::uint64_t countOffset,
			std::uint32_t maxDrawCount, std::uint32_t stride) override;
		void DrawIndirect(Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) override;
		void DrawIndirectCount(Rhi::Buffer& arguments, std::uint64_t offset, Rhi::Buffer& count, std::uint64_t countOffset,
			std::uint32_t maxDrawCount, std::uint32_t stride) override;
		void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;

		void ResetQueries(Rhi::QueryPool& pool, std::uint32_t first, std::uint32_t count) override;
		void WriteTimestamp(Rhi::QueryPool& pool, std::uint32_t index, Rhi::TimestampStage stage) override;

		ID3D12GraphicsCommandList* GetList() const { return list.Get(); }

	  private:
		struct TextureTransition
		{
			D3D12Texture* Texture = nullptr;
			Rhi::ResourceState Before{};
			Rhi::ResourceState After{};
			Rhi::TextureSubresourceRange Range{};
		};

		struct BufferTransition
		{
			D3D12Buffer* Buffer = nullptr;
			Rhi::ResourceState Before{};
			Rhi::ResourceState After{};
		};

		void RecordEnhanced(std::span<const BufferTransition> buffers, std::span<const TextureTransition> textures);
		void RecordLegacy(std::span<const BufferTransition> buffers, std::span<const TextureTransition> textures);
		void RecordBarriers(std::span<const BufferTransition> buffers, std::span<const TextureTransition> textures);
		void ApplyRootBindings(bool compute);
		// Root bindings plus the draw base (first vertex, first instance) for draw ids.
		void PrepareDraw(std::uint32_t baseVertex, std::uint32_t baseInstance);
		std::uint32_t VertexStride(std::uint32_t slot) const;
		ID3D12CommandSignature* GetIndirectSignature(int kind, std::uint32_t stride);
		void ResolveTimestamps();
		// A device-local buffer kept until the list is reset (repacked copy rows).
		D3D12Buffer& Scratch(std::uint64_t size);
		// Placement-aligned room for repacked copy rows in the list's staging blocks.
		std::pair<D3D12Buffer*, std::uint64_t> Stage(std::uint64_t size);
		// Records the texture copies waiting on repacked rows, behind one barrier.
		void FlushCopies();
		void RetireStaging();
		void ExecuteIndirect(int kind, Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t maxCount, Rhi::Buffer* count,
			std::uint64_t countOffset, std::uint32_t stride);

		std::shared_ptr<D3D12DeviceState> state;
		Rhi::QueueType type;
		std::shared_ptr<D3D12AllocatorSet> allocators;
		ID3D12CommandAllocator* allocator = nullptr;
		ComPtr<ID3D12GraphicsCommandList> list;
		ComPtr<ID3D12GraphicsCommandList7> list7; // Enhanced barriers.
		bool recording = false;
		bool rendering = false;

		// Bound state: root signature parameters are applied before each draw/dispatch.
		D3D12PipelineLayout* graphicsLayout = nullptr;
		D3D12PipelineLayout* computeLayout = nullptr;
		// Bound tables per bind point (Rhi::DescriptorBindingState: kept across compatible
		// pipelines, as on every backend); the last pipeline bound picks the bind point.
		Rhi::DescriptorBindingState graphicsTables;
		Rhi::DescriptorBindingState computeTables;
		bool computeActive = false;
		std::array<std::uint32_t, 32> pushConstants{};
		bool graphicsDirty = false;
		bool computeDirty = false;

		// What each bind point's root signature last received, by space: setting a root
		// argument again with the same value is redundant work for the runtime and driver.
		// A root signature change leaves every root argument undefined, so it clears these.
		struct AppliedSpace
		{
			const class D3D12DescriptorTable* Table = nullptr;
			std::uint64_t Version = 0;
		};

		std::vector<AppliedSpace> graphicsApplied;
		std::vector<AppliedSpace> computeApplied;
		bool graphicsPushDirty = false;
		bool computePushDirty = false;
		ID3D12PipelineState* boundPipeline = nullptr;
		D3D12_PRIMITIVE_TOPOLOGY boundTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		std::uint32_t boundStencilReference = ~0u;
		class D3D12GraphicsPipeline* graphicsPipeline = nullptr;
		std::array<D3D12_VERTEX_BUFFER_VIEW, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexBuffers{};
		std::vector<std::unique_ptr<D3D12Buffer>> scratch;

		// Buffer-to-texture copies whose rows were repacked: the row copies are recorded at
		// once, the texture copies at the next command that could observe them.
		struct StagingBlock
		{
			std::unique_ptr<D3D12Buffer> Buffer;
			std::uint64_t Used = 0;
			bool Written = false; // Holds rows of pending copies.
		};

		struct PendingCopy
		{
			D3D12_TEXTURE_COPY_LOCATION To{};
			D3D12_TEXTURE_COPY_LOCATION From{};
			UINT X = 0;
			UINT Y = 0;
			UINT Z = 0;
		};

		std::vector<StagingBlock> staging;
		std::vector<PendingCopy> pendingCopies;

		// Barrier scratch kept between calls (cleared, capacity reused). Separate per function:
		// RecordBarriers flushes pending copies first, which records barriers of its own while
		// a caller's transitions are still being read.
		std::vector<BufferTransition> transitionBuffers;
		std::vector<TextureTransition> transitionTextures;
		std::vector<D3D12_BUFFER_BARRIER> enhancedBuffers;
		std::vector<D3D12_TEXTURE_BARRIER> enhancedTextures;
		std::vector<D3D12_RESOURCE_BARRIER> legacyBarriers;
		std::vector<std::pair<D3D12QueryPool*, std::uint32_t>> pendingTimestamps;
	};

	class D3D12CommandPool final : public Rhi::CommandPool
	{

	  public:
		D3D12CommandPool(std::shared_ptr<D3D12DeviceState> state, Rhi::QueueType type);

		std::uintptr_t GetNativeHandle() const override { return 0; }

		std::unique_ptr<Rhi::CommandList> CreateCommandList() override;
		void Reset() override;

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::QueueType type;
		std::shared_ptr<D3D12AllocatorSet> allocators;
	};

} // namespace Swim::RhiD3D12
