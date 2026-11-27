#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/Internal/D3D12Common.h"
#include "Engine/Systems/Renderer/RHI/RhiDeviceDiagnostics.h"
#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Swim::RhiD3D12
{

	// What the adapter's runtime and driver support, queried once per adapter.
	struct D3D12Features
	{
		D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_12_0;
		D3D_SHADER_MODEL ShaderModel = D3D_SHADER_MODEL_5_1;
		D3D12_RESOURCE_BINDING_TIER BindingTier = D3D12_RESOURCE_BINDING_TIER_1;
		D3D12_RESOURCE_HEAP_TIER HeapTier = D3D12_RESOURCE_HEAP_TIER_1;
		D3D12_MESH_SHADER_TIER MeshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
		D3D12_RAYTRACING_TIER RaytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
		bool EnhancedBarriers = false;
		// D3D12_FEATURE_DATA_D3D12_OPTIONS13::UnrestrictedBufferTextureCopyPitchSupported: buffer
		// footprints need only whole-texel (or block) offsets and row pitches, not 512 and 256.
		bool UnrestrictedCopyPitch = false;
		bool WaveOps = false;
		std::uint32_t WaveLaneCountMin = 0;
		std::uint32_t WaveLaneCountMax = 0;
		bool TypedUavLoadAdditionalFormats = false;
		bool CopyQueueTimestamps = false;
		bool RootSignature11 = false;
		bool Tearing = false;

		// SV_StartVertexLocation / SV_StartInstanceLocation (shader model 6.8).
		bool DrawStartLocations() const { return ShaderModel >= static_cast<D3D_SHADER_MODEL>(0x68); }

		// ResourceDescriptorHeap / SamplerDescriptorHeap indexing (shader model 6.6, binding tier 3).
		bool DynamicResources() const { return ShaderModel >= D3D_SHADER_MODEL_6_6 && BindingTier >= D3D12_RESOURCE_BINDING_TIER_3; }
	};

	class D3D12PlacedHeaps;

	class D3D12DeferredReleases;

	// How far each queue's submissions have got: every queue signals its own fence after each
	// submission. Needed() is what the GPU must complete before something released now is
	// unused: everything each queue has submitted, and on the direct queue one submission
	// more once it has submitted at all (the frame being recorded may still use it).
	class D3D12GpuProgress
	{

	  public:
		std::size_t Register(ComPtr<ID3D12Fence> fence, bool direct);

		void Submitted(std::size_t queue, std::uint64_t value);

		std::vector<std::uint64_t> Needed();

		bool Passed(const std::vector<std::uint64_t>& needed);

	  private:
		std::mutex mutex;
		std::vector<ComPtr<ID3D12Fence>> fences;
		std::vector<std::uint64_t> submitted;
		std::vector<bool> directs;
	};

	// One placed resource's range of a default-heap block; the range returns to the block when
	// the placement is destroyed, which must follow the resource's release.
	class D3D12Placement
	{

	  public:
		D3D12Placement() = default;
		D3D12Placement(const D3D12Placement&) = delete;

		D3D12Placement(D3D12Placement&& other) noexcept { *this = std::move(other); }

		D3D12Placement& operator=(D3D12Placement&& other) noexcept
		{
			std::swap(owner, other.owner);
			std::swap(block, other.block);
			std::swap(offset, other.offset);
			std::swap(size, other.size);
			return *this;
		}

		~D3D12Placement();

		explicit operator bool() const { return owner != nullptr; }

	  private:
		friend class D3D12PlacedHeaps;
		D3D12PlacedHeaps* owner = nullptr;
		void* block = nullptr;
		std::uint64_t offset = 0;
		std::uint64_t size = 0;
	};

	// Default-heap blocks that placed buffers and textures suballocate, so creating and dropping a
	// resource costs a range, not a heap. With resource heap tier 2 one block kind holds buffers and
	// textures alike; with tier 1, buffers and non-attachment textures get separate blocks.
	// A range is reused as soon as its resource is released. The RHI already requires a resource
	// to outlive its GPU work (as suballocated Vulkan memory does), so the next resource in that
	// range is first used in a later ExecuteCommandLists, which orders aliased accesses as an
	// aliasing barrier would (CreatePlacedResource, "Notes on the aliasing barrier"). Render-target
	// and depth-stencil textures would also need a clear, discard or copy before first use, so they
	// stay committed, as do multisampled textures (4 MiB alignment) and resources above half a block.
	class D3D12PlacedHeaps
	{

	  public:
		static constexpr std::uint64_t BlockSize = 64ull << 20;

		explicit D3D12PlacedHeaps(D3D12GpuProgress& progressValue) : progress(progressValue) {}

		// A range for a resource with this allocation info, or an empty placement when it does not fit
		// a block; heap and offset receive where to place it.
		D3D12Placement Allocate(ID3D12Device* device, bool tier2, bool buffer, const D3D12_RESOURCE_ALLOCATION_INFO& info,
			ID3D12Heap*& heap, std::uint64_t& offset);

		std::uint64_t GetBlockCount();

	  private:
		friend class D3D12Placement;

		struct Block
		{
			ComPtr<ID3D12Heap> Heap;
			std::map<std::uint64_t, std::uint64_t> FreeRanges; // offset -> size
			std::uint64_t Used = 0;
			std::uint32_t Kind = 0;
			std::vector<std::uint64_t> EmptyAt; // Progress when it last became empty.
		};

		void Free(D3D12Placement& placement) noexcept;

		// Releases empty blocks (not the last of a kind) once the GPU has passed the work that
		// could still touch them, even through a resource freed too early.
		void Sweep(std::vector<std::unique_ptr<Block>>& list) noexcept;

		D3D12GpuProgress& progress;

		std::mutex mutex;
		std::array<std::vector<std::unique_ptr<Block>>, 2> blocks; // Tier 2 uses kind 0 only.
	};

	// Fills features and the RHI capabilities from a device of the adapter.
	D3D12Features QueryFeatures(ID3D12Device* device, IDXGIFactory6* factory);
	Rhi::GraphicsCapabilities ToCapabilities(const D3D12Features& features, ID3D12Device* device);
	std::string DescribeFeatures(const D3D12Features& features);

	// A descriptor heap handing out contiguous ranges (first fit, freed ranges merge).
	class D3D12DescriptorHeap
	{

	  public:
		static constexpr std::uint32_t Invalid = UINT32_MAX;

		D3D12DescriptorHeap(
			ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible, std::string_view name);

		std::uint32_t Allocate(std::uint32_t count = 1);
		void Free(std::uint32_t index, std::uint32_t count = 1);

		D3D12_CPU_DESCRIPTOR_HANDLE Cpu(std::uint32_t index) const { return { cpuStart.ptr + std::size_t(index) * increment }; }

		D3D12_GPU_DESCRIPTOR_HANDLE Gpu(std::uint32_t index) const { return { gpuStart.ptr + std::uint64_t(index) * increment }; }

		ID3D12DescriptorHeap* Get() const { return heap.Get(); }

		std::uint32_t GetCapacity() const { return capacity; }

	  private:
		ComPtr<ID3D12DescriptorHeap> heap;
		std::uint32_t increment = 0;
		std::uint32_t capacity = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
		std::mutex mutex;
		std::map<std::uint32_t, std::uint32_t> freeRanges; // start -> count
	};

	// Everything the backend objects of one device share. Objects hold it by shared_ptr so it
	// outlives every object of the device.
	struct D3D12DeviceState
	{
		ComPtr<IDXGIFactory6> Factory;
		ComPtr<IDXGIAdapter4> Adapter;
		ComPtr<ID3D12Device5> Device;
		ComPtr<ID3D12Device10> Device10; // Enhanced barriers; null without them.
		D3D12Features Features;
		std::shared_ptr<Rhi::DiagnosticLog> Log;
		std::shared_ptr<Rhi::DeviceDiagnostics> Diagnostics = std::make_shared<Rhi::DeviceDiagnostics>(true);
		bool Debug = false;
		bool Uma = false;
		// Live committed resources per DXGI memory segment group (0 local, 1 non-local).
		std::array<std::atomic<std::uint64_t>, 2> AllocationCount{};
		std::array<std::atomic<std::uint64_t>, 2> AllocationBytes{};

		std::unique_ptr<D3D12DescriptorHeap> RenderTargets;
		std::unique_ptr<D3D12DescriptorHeap> DepthStencils;
		std::unique_ptr<D3D12DescriptorHeap> CpuViews; // CBV/SRV/UAV staging (copy sources, clears).
		std::unique_ptr<D3D12DescriptorHeap> CpuSamplers;
		std::unique_ptr<D3D12DescriptorHeap> Views;	   // Shader-visible CBV/SRV/UAV.
		std::unique_ptr<D3D12DescriptorHeap> Samplers; // Shader-visible samplers.

		// The pipeline disk cache: PSOs by a hash of everything they are built from. The library
		// reads from Blob, which lives as long as the library.
		std::mutex PipelineLibraryMutex;
		ComPtr<ID3D12PipelineLibrary1> PipelineLibrary;
		std::vector<std::byte> PipelineLibraryBlob;
		bool PipelineLibraryLoaded = false;

		std::mutex SignatureMutex;
		std::map<std::pair<int, std::uint32_t>, ComPtr<ID3D12CommandSignature>> CommandSignatures;

		// Debug-layer messages: delivered by the info-queue callback, or, where the callback
		// interface is missing, read from this queue after every submission.
		bool EchoMessages = true;
		ComPtr<ID3D12InfoQueue> PolledMessages;
		std::mutex PolledMessagesMutex;

		// Records the loss once, with the removal reason and DRED breadcrumbs when available.
		void RecordLoss(std::string_view operation, HRESULT result) const;

		// Moves the polled debug-layer messages into the log (nothing without PolledMessages).
		void DrainMessages();

		// An error the backend itself finds in a call, reported like a debug-layer message the
		// first time its key is seen: "[RHI diagnostic] D3D12 ERROR (RHI check) <id>: <text>".
		void ReportOnce(std::string key, std::string_view id, std::string_view text);
		D3D12GpuProgress Progress;
		D3D12PlacedHeaps PlacedHeaps{ Progress };
		// Resources released while the GPU may still use them (after PlacedHeaps: their ranges
		// return to it). Created with the state.
		std::unique_ptr<D3D12DeferredReleases> Deferred = std::make_unique<D3D12DeferredReleases>(Progress);
		std::mutex ReportedMutex;
		std::set<std::string> Reported;

		// The command signature for an indirect argument kind (0 draw, 1 indexed draw, 2 dispatch) and stride.
		ID3D12CommandSignature* GetCommandSignature(int kind, std::uint32_t stride);
	};

	// One debug-layer message into the log, and to stderr (the engine log) when echoed, as
	// "[RHI diagnostic] D3D12 ERROR #<id>: <description>". Info messages are dropped.
	void RecordDebugMessage(Rhi::DiagnosticLog& log, bool echo, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
		std::string_view description) noexcept;

	// One committed resource, counted in its memory segment for as long as the record lives.
	class D3D12MemoryRecord
	{

	  public:
		D3D12MemoryRecord() = default;

		D3D12MemoryRecord(D3D12DeviceState& stateValue, const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heap)
			: state(&stateValue), segment(heap == D3D12_HEAP_TYPE_DEFAULT || stateValue.Uma ? 0u : 1u),
			  bytes(stateValue.Device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes)
		{
			state->AllocationCount[segment] += 1;
			state->AllocationBytes[segment] += bytes;
		}

		D3D12MemoryRecord(const D3D12MemoryRecord&) = delete;

		D3D12MemoryRecord(D3D12MemoryRecord&& other) noexcept { *this = std::move(other); }

		D3D12MemoryRecord& operator=(D3D12MemoryRecord&& other) noexcept
		{
			std::swap(state, other.state);
			std::swap(segment, other.segment);
			std::swap(bytes, other.bytes);
			return *this;
		}

		~D3D12MemoryRecord()
		{
			if (state)
			{
				state->AllocationCount[segment] -= 1;
				state->AllocationBytes[segment] -= bytes;
			}
		}

		bool Owned() const { return state != nullptr; }

	  private:
		D3D12DeviceState* state = nullptr;
		std::uint32_t segment = 0;
		std::uint64_t bytes = 0;
	};

	// The default for owned resources released mid-frame: the resource, its placed range and
	// its memory record are kept until the GPU has passed every submission that may use them
	// (D3D12GpuProgress::Needed), then released at a later submission or wait. With nothing
	// in flight they go at once.
	class D3D12DeferredReleases
	{

	  public:
		explicit D3D12DeferredReleases(D3D12GpuProgress& progressValue) : progress(progressValue) {}

		void Release(ComPtr<ID3D12Resource> resource, D3D12Placement placement, D3D12MemoryRecord memory) noexcept;

		void Collect() noexcept;

		std::size_t GetPending();

	  private:
		struct Entry
		{
			ComPtr<ID3D12Resource> Resource;
			D3D12Placement Placement;
			D3D12MemoryRecord Memory;
			std::vector<std::uint64_t> Needed;
		};

		D3D12GpuProgress& progress;
		std::mutex mutex;
		std::vector<Entry> entries;
	};

} // namespace Swim::RhiD3D12
