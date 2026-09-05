#pragma once

#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Platform
{
	class Window;
}

namespace Swim::Rhi
{

	class Adapter;
	class Device;
	class Buffer;
	class Texture;
	class TextureView;
	class Sampler;
	class ShaderProgram;
	class PipelineLayout;
	class GraphicsPipeline;
	class ComputePipeline;
	class DescriptorTable;
	class CommandList;
	class Semaphore;
	class Fence;
	class Timeline;
	class QueryPool;

	inline constexpr std::uint64_t InfiniteTimeout = std::numeric_limits<std::uint64_t>::max();

	enum class QueueType : std::uint8_t
	{
		Graphics,
		Compute,
		Transfer,
	};

	enum class IndexType : std::uint8_t
	{
		Uint16,
		Uint32,
	};

	enum class QueryType : std::uint8_t
	{
		Timestamp,
		Occlusion,
		PipelineStatistics,
	};

	struct Extent2D
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	struct Extent3D
	{
		std::uint32_t Width = 1;
		std::uint32_t Height = 1;
		std::uint32_t Depth = 1;
	};

	struct Offset3D
	{
		std::int32_t X = 0;
		std::int32_t Y = 0;
		std::int32_t Z = 0;
	};

	struct BufferDesc
	{
		std::uint64_t Size = 0;
		BufferUsage Usage = BufferUsage::None;
		MemoryPreference Memory = MemoryPreference::DeviceLocal;
		std::string_view DebugName;
	};

	struct TextureDesc
	{
		TextureDimension Dimension = TextureDimension::Texture2D;
		Extent3D Extent{};
		Format PixelFormat = Format::Undefined;
		TextureUsage Usage = TextureUsage::None;
		std::uint32_t MipLevels = 1;
		std::uint32_t ArrayLayers = 1;
		SampleCount Samples = SampleCount::X1;
		std::string_view DebugName;
	};

	struct TextureViewDesc
	{
		TextureViewDimension Dimension = TextureViewDimension::Texture2D;
		Format PixelFormat = Format::Undefined;
		std::uint32_t BaseMipLevel = 0;
		std::uint32_t MipLevelCount = 1;
		std::uint32_t BaseArrayLayer = 0;
		std::uint32_t ArrayLayerCount = 1;
		std::string_view DebugName;
	};

	struct SamplerDesc
	{
		Filter MinFilter = Filter::Linear;
		Filter MagFilter = Filter::Linear;
		Filter MipFilter = Filter::Linear;
		SamplerAddressMode AddressU = SamplerAddressMode::Repeat;
		SamplerAddressMode AddressV = SamplerAddressMode::Repeat;
		SamplerAddressMode AddressW = SamplerAddressMode::Repeat;
		float MipLodBias = 0.0f;
		float MinLod = 0.0f;
		float MaxLod = 1000.0f;
		float MaxAnisotropy = 1.0f;
		bool EnableAnisotropy = false;
		bool EnableComparison = false;
		CompareOp Comparison = CompareOp::Always;
		std::string_view DebugName;
	};

	struct ShaderStageArtifact
	{
		ShaderStageMask Stage = ShaderStageMask::None;
		std::string_view EntryPoint;
		std::span<const std::byte> Bytecode;
	};

	struct PushConstantRange
	{
		std::uint32_t Offset = 0;
		std::uint32_t Size = 0;
		ShaderStageMask Stages = ShaderStageMask::None;
	};

	struct ShaderProgramInterfaceDesc
	{
		std::span<const DescriptorSchemaDesc> DescriptorSchemas;
		std::span<const PushConstantRange> PushConstants;
	};

	struct ShaderProgramInterface
	{
		std::vector<DescriptorSchemaDesc> DescriptorSchemas;
		std::vector<PushConstantRange> PushConstants;
	};

	struct ShaderProgramDesc
	{
		std::span<const ShaderStageArtifact> Stages;
		ShaderProgramInterfaceDesc Interface{};
		std::string_view DebugName;
	};

	struct PipelineLayoutDesc
	{
		ShaderProgram* Program = nullptr;
		std::string_view DebugName;
	};

	struct DescriptorTableDesc
	{
		PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		std::uint32_t VariableDescriptorCount = 0;
		std::string_view DebugName;
	};

	struct GraphicsPipelineDesc
	{
		ShaderProgram* Program = nullptr;
		PipelineLayout* Layout = nullptr;
		std::span<const Format> ColorFormats;
		std::span<const BlendAttachmentState> BlendAttachments;
		Format DepthStencilFormat = Format::Undefined;
		PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
		RasterState Raster{};
		DepthStencilState DepthStencil{};
		SampleCount Samples = SampleCount::X1;
		std::string_view DebugName;
	};

	struct ComputePipelineDesc
	{
		ShaderProgram* Program = nullptr;
		PipelineLayout* Layout = nullptr;
		std::string_view EntryPoint;
		std::string_view DebugName;
	};

	struct SwapchainDesc
	{
		Format PreferredFormat = Format::BGRA8UnormSrgb;
		std::uint32_t ImageCount = 3;
		bool Vsync = true;
		bool Hdr = false;
	};

	struct BufferCopyRegion
	{
		std::uint64_t SourceOffset = 0;
		std::uint64_t DestinationOffset = 0;
		std::uint64_t Size = 0;
	};

	struct TextureSubresource
	{
		std::uint32_t MipLevel = 0;
		std::uint32_t ArrayLayer = 0;
	};

	struct TextureSubresourceRange
	{
		std::uint32_t BaseMipLevel = 0;
		std::uint32_t MipLevelCount = 0xffffffffu;
		std::uint32_t BaseArrayLayer = 0;
		std::uint32_t ArrayLayerCount = 0xffffffffu;
	};

	struct TextureCopyRegion
	{
		TextureSubresource Source{};
		TextureSubresource Destination{};
		Offset3D SourceOffset{};
		Offset3D DestinationOffset{};
		Extent3D Extent{};
	};

	struct ClearColor
	{
		std::array<float, 4> Value{ 0.0f, 0.0f, 0.0f, 0.0f };
	};

	struct RenderingAttachmentDesc
	{
		TextureView* View = nullptr;
		LoadOp Load = LoadOp::Load;
		StoreOp Store = StoreOp::Store;
		ClearColor Clear{};
	};

	struct DepthStencilAttachmentDesc
	{
		TextureView* View = nullptr;
		LoadOp DepthLoad = LoadOp::Load;
		StoreOp DepthStore = StoreOp::Store;
		float ClearDepth = 1.0f;
		std::uint32_t ClearStencil = 0;
	};

	struct RenderingDesc
	{
		std::span<const RenderingAttachmentDesc> ColorAttachments;
		const DepthStencilAttachmentDesc* DepthStencilAttachment = nullptr;
		Extent2D RenderArea{};
	};

	struct TimelinePoint
	{
		Timeline* Semaphore = nullptr;
		std::uint64_t Value = 0;
	};

	struct SubmitDesc
	{
		std::span<CommandList* const> CommandLists;
		std::span<Semaphore* const> WaitSemaphores;
		std::span<Semaphore* const> SignalSemaphores;
		std::span<const TimelinePoint> WaitTimelines;
		std::span<const TimelinePoint> SignalTimelines;
		Fence* CompletionFence = nullptr;
	};

	struct DescriptorWrite
	{
		std::uint32_t Binding = 0;
		std::uint32_t ArrayIndex = 0;
		Buffer* BufferResource = nullptr;
		TextureView* TextureResource = nullptr;
		Sampler* SamplerResource = nullptr;
		std::uint64_t BufferOffset = 0;
		std::uint64_t BufferRange = 0;
	};

	struct QueryPoolDesc
	{
		QueryType Type = QueryType::Timestamp;
		std::uint32_t Count = 0;
		std::string_view DebugName;
	};

	struct AdapterInfo
	{
		std::string Name;
		std::uint32_t VendorId = 0;
		std::uint32_t DeviceId = 0;
		std::uint64_t DedicatedVideoMemory = 0;
		GraphicsCapabilities Capabilities{};
	};

	class RhiObject
	{
	public:
		RhiObject() = default;
		virtual ~RhiObject() = default;

		RhiObject(const RhiObject&) = delete;
		RhiObject& operator=(const RhiObject&) = delete;

		virtual std::uintptr_t GetNativeHandle() const = 0;
	};

	class Buffer : public RhiObject
	{
	public:
		virtual const BufferDesc& GetDesc() const = 0;
	};

	class Texture : public RhiObject
	{
	public:
		virtual const TextureDesc& GetDesc() const = 0;
	};

	class TextureView : public RhiObject
	{
	public:
		virtual Texture& GetTexture() const = 0;
		virtual const TextureViewDesc& GetDesc() const = 0;
	};

	class Sampler : public RhiObject
	{
	public:
		virtual const SamplerDesc& GetDesc() const = 0;
	};

	class ShaderProgram : public RhiObject
	{
	public:
		virtual const ShaderProgramInterface& GetInterface() const = 0;
	};

	class PipelineLayout : public RhiObject
	{
	public:
		virtual ShaderProgram& GetProgram() const = 0;
		virtual const ShaderProgramInterface& GetInterface() const = 0;
	};

	class GraphicsPipeline : public RhiObject
	{
	};

	class ComputePipeline : public RhiObject
	{
	};

	class DescriptorTable : public RhiObject
	{
	public:
		virtual PipelineLayout& GetLayout() const = 0;
		virtual std::uint32_t GetSpace() const = 0;
		virtual void Write(std::span<const DescriptorWrite> writes) = 0;
	};

	class Semaphore : public RhiObject
	{
	};

	class Fence : public RhiObject
	{
	public:
		virtual bool IsSignaled() const = 0;
		virtual bool Wait(std::uint64_t timeoutNanoseconds = InfiniteTimeout) = 0;
		virtual void Reset() = 0;
	};

	class Timeline : public RhiObject
	{
	public:
		virtual std::uint64_t GetCompletedValue() const = 0;
		virtual bool Wait(std::uint64_t value, std::uint64_t timeoutNanoseconds = InfiniteTimeout) = 0;
	};

	class QueryPool : public RhiObject
	{
	public:
		virtual const QueryPoolDesc& GetDesc() const = 0;
	};

	class CommandList : public RhiObject
	{
	public:
		virtual void Begin() = 0;
		virtual void End() = 0;
		virtual void Transition(Buffer& buffer, ResourceState before, ResourceState after) = 0;
		virtual void Transition(Texture& texture, ResourceState before, ResourceState after, const TextureSubresourceRange& range = {}) = 0;
		virtual void CopyBuffer(Buffer& source, Buffer& destination, const BufferCopyRegion& region) = 0;
		virtual void CopyTexture(Texture& source, Texture& destination, const TextureCopyRegion& region) = 0;
		virtual void BeginRendering(const RenderingDesc& desc) = 0;
		virtual void EndRendering() = 0;
		virtual void BindGraphicsPipeline(GraphicsPipeline& pipeline) = 0;
		virtual void BindComputePipeline(ComputePipeline& pipeline) = 0;
		virtual void BindDescriptorTable(std::uint32_t space, DescriptorTable& table) = 0;
		virtual void SetViewport(const Viewport& viewport) = 0;
		virtual void SetScissor(const ScissorRect& scissor) = 0;
		virtual void BindVertexBuffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) = 0;
		virtual void BindIndexBuffer(Buffer& buffer, std::uint64_t offset, IndexType type) = 0;
		virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0) = 0;
		virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) = 0;
		virtual void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
		virtual void WriteTimestamp(QueryPool& pool, std::uint32_t index) = 0;
	};

	class CommandPool : public RhiObject
	{
	public:
		virtual std::unique_ptr<CommandList> CreateCommandList() = 0;
		virtual void Reset() = 0;
	};

	class Queue : public RhiObject
	{
	public:
		virtual QueueType GetType() const = 0;
		virtual void Submit(const SubmitDesc& desc) = 0;
		virtual void WaitIdle() = 0;
	};

	struct SwapchainAcquireResult
	{
		std::uint32_t ImageIndex = 0;
		bool OutOfDate = false;
		bool Suboptimal = false;
	};

	class Swapchain : public RhiObject
	{
	public:
		virtual Format GetFormat() const = 0;
		virtual Extent2D GetExtent() const = 0;
		virtual std::uint32_t GetImageCount() const = 0;
		virtual TextureView& GetImageView(std::uint32_t imageIndex) = 0;
		virtual SwapchainAcquireResult AcquireNextImage(Semaphore& signalSemaphore, Fence* signalFence = nullptr) = 0;
		virtual bool Present(Queue& queue, std::uint32_t imageIndex, std::span<Semaphore* const> waits) = 0;
		virtual void Resize(Extent2D extent) = 0;
	};

	class Device : public RhiObject
	{
	public:
		virtual const AdapterInfo& GetAdapterInfo() const = 0;
		virtual Queue& GetQueue(QueueType type) = 0;

		virtual std::unique_ptr<Swapchain> CreateSwapchain(Platform::Window& window, const SwapchainDesc& desc) = 0;
		virtual std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;
		virtual std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
		virtual std::unique_ptr<TextureView> CreateTextureView(Texture& texture, const TextureViewDesc& desc) = 0;
		virtual std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) = 0;
		virtual std::unique_ptr<ShaderProgram> CreateShaderProgram(const ShaderProgramDesc& desc) = 0;
		virtual std::unique_ptr<PipelineLayout> CreatePipelineLayout(const PipelineLayoutDesc& desc) = 0;
		virtual std::unique_ptr<GraphicsPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
		virtual std::unique_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
		virtual std::unique_ptr<DescriptorTable> CreateDescriptorTable(const DescriptorTableDesc& desc) = 0;
		virtual std::unique_ptr<CommandPool> CreateCommandPool(QueueType queueType) = 0;
		virtual std::unique_ptr<Semaphore> CreateSemaphore() = 0;
		virtual std::unique_ptr<Fence> CreateFence(bool signaled = false) = 0;
		virtual std::unique_ptr<Timeline> CreateTimeline(std::uint64_t initialValue = 0) = 0;
		virtual std::unique_ptr<QueryPool> CreateQueryPool(const QueryPoolDesc& desc) = 0;
		virtual void WaitIdle() = 0;
	};

	class Adapter : public RhiObject
	{
	public:
		virtual const AdapterInfo& GetInfo() const = 0;
		virtual std::unique_ptr<Device> CreateDevice() = 0;
	};

	class GraphicsSystem
	{
	public:
		virtual ~GraphicsSystem() = default;
		virtual std::uint32_t GetAdapterCount() const = 0;
		virtual Adapter& GetAdapter(std::uint32_t adapterIndex) = 0;
	};

} // namespace Swim::Rhi
