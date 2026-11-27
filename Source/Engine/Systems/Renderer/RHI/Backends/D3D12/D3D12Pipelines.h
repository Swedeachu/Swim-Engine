#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"

#include <map>
#include <optional>
#include <vector>

namespace Swim::RhiD3D12
{

	// Registers the backend reserves in every root signature: the draw base (first vertex, first
	// instance) of graphics pipelines for shaders without SV_StartVertexLocation, and the push
	// constants of programs whose reflection does not place them.
	inline constexpr UINT DrawBaseRegisterSpace = 1000;
	inline constexpr UINT FallbackPushConstantSpace = 999;

	class D3D12ShaderProgram final : public Rhi::ShaderProgram
	{

	  public:
		struct Stage
		{
			Rhi::ShaderStageMask Mask = Rhi::ShaderStageMask::None;
			std::string EntryPoint;
			std::vector<std::byte> Bytecode; // One DXIL container.
		};

		// Null when a stage carries no DXIL container of its kind.
		static std::unique_ptr<D3D12ShaderProgram> Create(std::shared_ptr<D3D12DeviceState> state, const Rhi::ShaderProgramDesc& desc);

		std::uintptr_t GetNativeHandle() const override { return 0; }

		const Rhi::ShaderProgramInterface& GetInterface() const override { return programInterface; }

		const std::vector<Stage>& GetStages() const { return stages; }

		const Stage* FindStage(Rhi::ShaderStageMask mask) const;

		const std::shared_ptr<D3D12DeviceState>& GetState() const { return state; }

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::ShaderProgramInterface programInterface;
		std::vector<Stage> stages;
	};

	// The root signature of a program: push constants and the draw base as root constants, then
	// per descriptor space one CBV/SRV/UAV table and one sampler table. Table slots follow the
	// space's bindings in order, so a table fits every layout that defines its space the same way.
	class D3D12PipelineLayout final : public Rhi::PipelineLayout
	{

	  public:
		struct Slot
		{
			Rhi::DescriptorBindingDesc Binding;
			bool Sampler = false;
			std::uint32_t Offset = 0; // Into the space's view or sampler range.
			std::optional<Rhi::NativeBinding> Native;
			// A root SRV/UAV instead of a table range: structured buffer views address whole
			// elements, root descriptors take the byte offset the RHI allows.
			int RootParameter = -1;
		};

		struct Space
		{
			std::uint32_t Index = 0;
			std::vector<Slot> Slots;
			std::uint32_t ViewCount = 0; // Capacity, runtime arrays included.
			std::uint32_t SamplerCount = 0;
			int ViewParameter = -1;
			int SamplerParameter = -1;
		};

		static std::unique_ptr<D3D12PipelineLayout> Create(std::shared_ptr<D3D12DeviceState> state, const Rhi::PipelineLayoutDesc& desc);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(rootSignature.Get()); }

		Rhi::ShaderProgram& GetProgram() const override { return program; }

		// The program interface with the explicit descriptor spaces in place of the reflected ones.
		const Rhi::ShaderProgramInterface& GetInterface() const override { return layoutInterface; }

		ID3D12RootSignature* GetRootSignature() const { return rootSignature.Get(); }

		const std::vector<std::byte>& GetRootSignatureBlob() const { return blob; }

		const Space* FindSpace(std::uint32_t space) const;

		const std::vector<Space>& GetSpaces() const { return spaces; }

		int GetPushParameter() const { return pushParameter; }

		std::uint32_t GetPushDwords() const { return pushDwords; }

		int GetDrawBaseParameter() const { return drawBaseParameter; }

		bool IsCompute() const { return compute; }

	  private:
		D3D12PipelineLayout(std::shared_ptr<D3D12DeviceState> state, D3D12ShaderProgram& program);

		std::shared_ptr<D3D12DeviceState> state;
		D3D12ShaderProgram& program;
		Rhi::ShaderProgramInterface layoutInterface;
		std::vector<Space> spaces;
		ComPtr<ID3D12RootSignature> rootSignature;
		std::vector<std::byte> blob;
		int pushParameter = -1;
		std::uint32_t pushDwords = 0;
		int drawBaseParameter = -1;
		bool compute = false;
	};

	class D3D12GraphicsPipeline final : public Rhi::GraphicsPipeline
	{

	  public:
		static std::unique_ptr<D3D12GraphicsPipeline> Create(
			std::shared_ptr<D3D12DeviceState> state, const Rhi::GraphicsPipelineDesc& desc);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(pipeline.Get()); }

		ID3D12PipelineState* GetPipeline() const { return pipeline.Get(); }

		D3D12PipelineLayout& GetLayout() const { return layout; }

		D3D_PRIMITIVE_TOPOLOGY GetTopology() const { return topology; }

		std::uint32_t GetStencilReference() const { return stencilReference; }

		std::uint32_t GetStride(std::uint32_t slot) const { return slot < strides.size() ? strides[slot] : 0u; }

	  private:
		D3D12GraphicsPipeline(D3D12PipelineLayout& layout) : layout(layout) {}

		D3D12PipelineLayout& layout;
		ComPtr<ID3D12PipelineState> pipeline;
		D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		std::uint32_t stencilReference = 0;
		std::vector<std::uint32_t> strides;
	};

	class D3D12ComputePipeline final : public Rhi::ComputePipeline
	{

	  public:
		static std::unique_ptr<D3D12ComputePipeline> Create(std::shared_ptr<D3D12DeviceState> state, const Rhi::ComputePipelineDesc& desc);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(pipeline.Get()); }

		ID3D12PipelineState* GetPipeline() const { return pipeline.Get(); }

		D3D12PipelineLayout& GetLayout() const { return layout; }

	  private:
		D3D12ComputePipeline(D3D12PipelineLayout& layout) : layout(layout) {}

		D3D12PipelineLayout& layout;
		ComPtr<ID3D12PipelineState> pipeline;
	};

	// One space's descriptors: a range of the shader-visible view heap and one of the sampler heap.
	class D3D12DescriptorTable final : public Rhi::DescriptorTable
	{

	  public:
		static std::unique_ptr<D3D12DescriptorTable> Create(std::shared_ptr<D3D12DeviceState> state, const Rhi::DescriptorTableDesc& desc);
		~D3D12DescriptorTable() override;

		std::uintptr_t GetNativeHandle() const override { return views; }

		Rhi::PipelineLayout& GetLayout() const override { return layout; }

		std::uint32_t GetSpace() const override { return space.Index; }

		void Write(std::span<const Rhi::DescriptorWrite> writes) override;

		bool ReopenForWrites() override { return true; }

		// The layout's description of this table's space (bindings, slot offsets).
		const D3D12PipelineLayout::Space& GetSpaceLayout() const { return space; }

		D3D12_GPU_DESCRIPTOR_HANDLE GetViews() const;
		D3D12_GPU_DESCRIPTOR_HANDLE GetSamplers() const;

		// The address written to a buffer binding (zero when unwritten), for root descriptors.
		D3D12_GPU_VIRTUAL_ADDRESS GetBufferAddress(std::uint32_t binding) const;

		// Counts Write calls: equal versions of one table hold the same root arguments.
		std::uint64_t GetVersion() const { return version; }

		// Every element written, except those of partially bound bindings (as on Vulkan):
		// only a complete table can be bound.
		bool IsComplete() const;

	  private:
		D3D12DescriptorTable(std::shared_ptr<D3D12DeviceState> state, D3D12PipelineLayout& layout, const D3D12PipelineLayout::Space& space);
		void WriteNull(const D3D12PipelineLayout::Slot& slot, std::uint32_t element);

		std::shared_ptr<D3D12DeviceState> state;
		D3D12PipelineLayout& layout;
		D3D12PipelineLayout::Space space;
		std::uint32_t views = D3D12DescriptorHeap::Invalid;
		std::uint32_t viewCount = 0;
		std::uint32_t samplers = D3D12DescriptorHeap::Invalid;
		std::uint32_t samplerCount = 0;
		std::map<std::uint32_t, D3D12_GPU_VIRTUAL_ADDRESS> bufferAddresses;
		std::vector<std::vector<bool>> written; // By slot, then element.
		std::uint64_t version = 0;
	};

	class D3D12QueryPool final : public Rhi::QueryPool
	{

	  public:
		D3D12QueryPool(std::shared_ptr<D3D12DeviceState> state, const Rhi::QueryPoolDesc& desc, Rhi::TimestampInfo info);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(heap.Get()); }

		const Rhi::QueryPoolDesc& GetDesc() const override { return desc; }

		Rhi::TimestampInfo GetTimestampInfo() const override { return info; }

		Rhi::QueryReadStatus ReadTimestamps(std::uint32_t first, std::span<Rhi::TimestampResult> results) override;

		ID3D12QueryHeap* GetHeap() const { return heap.Get(); }

		ID3D12Resource* GetReadback() const { return readback.Get(); }

		D3D12_QUERY_TYPE GetQueryType() const { return type; }

		// Recording order: a reset makes entries unavailable until written again.
		void MarkReset(std::uint32_t first, std::uint32_t count);
		void MarkWritten(std::uint32_t index);

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::QueryPoolDesc desc;
		std::string name;
		Rhi::TimestampInfo info;
		D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_TIMESTAMP;
		ComPtr<ID3D12QueryHeap> heap;
		ComPtr<ID3D12Resource> readback;
		std::mutex mutex;
		std::vector<bool> written;
	};

	// The pipeline disk cache (ID3D12PipelineLibrary).
	Rhi::PipelineCacheLoadStatus LoadD3D12PipelineCache(D3D12DeviceState& state, std::span<const std::byte> data);
	Rhi::PipelineCacheData GetD3D12PipelineCacheData(D3D12DeviceState& state);

} // namespace Swim::RhiD3D12
