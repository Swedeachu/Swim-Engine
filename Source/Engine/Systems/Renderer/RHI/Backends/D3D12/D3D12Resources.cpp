#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	namespace
	{

		bool Has(Rhi::BufferUsage usage, Rhi::BufferUsage flag)
		{
			return Rhi::EnumAnd(usage, flag) != Rhi::BufferUsage::None;
		}

		bool Has(Rhi::TextureUsage usage, Rhi::TextureUsage flag)
		{
			return Rhi::EnumAnd(usage, flag) != Rhi::TextureUsage::None;
		}

		D3D12_HEAP_TYPE HeapType(Rhi::MemoryPreference memory)
		{
			switch (memory)
			{
			case Rhi::MemoryPreference::CpuToGpu:
				return D3D12_HEAP_TYPE_UPLOAD;
			case Rhi::MemoryPreference::GpuToCpu:
				return D3D12_HEAP_TYPE_READBACK;
			default:
				return D3D12_HEAP_TYPE_DEFAULT;
			}
		}

		// A default-heap resource placed in a shared block when it can be (see D3D12PlacedHeaps), else
		// committed; through the enhanced-barrier entry points when present (layouts), else the legacy
		// ones (states).
		ComPtr<ID3D12Resource> CreateResource(D3D12DeviceState& state, D3D12_HEAP_TYPE heapType, const D3D12_RESOURCE_DESC& desc,
			D3D12_RESOURCE_STATES legacyState, D3D12_BARRIER_LAYOUT layout, std::string_view what, D3D12Placement& placement)
		{
			D3D12_HEAP_PROPERTIES heap{ heapType };
			ComPtr<ID3D12Resource> resource;
			const bool buffer = desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
			// SWIM_D3D12_COMMITTED=1 makes every resource committed (diagnosing resource lifetimes).
			static const bool committedOnly = []
			{
				const char* value = std::getenv("SWIM_D3D12_COMMITTED");
				return value && std::strcmp(value, "1") == 0;
			}();
			const bool placeable = !committedOnly && heapType == D3D12_HEAP_TYPE_DEFAULT && desc.SampleDesc.Count == 1 &&
				(desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == 0;

			if (placeable)
			{
				ID3D12Heap* block = nullptr;
				std::uint64_t offset = 0;
				auto range = state.PlacedHeaps.Allocate(state.Device.Get(), state.Features.HeapTier >= D3D12_RESOURCE_HEAP_TIER_2, buffer,
					state.Device->GetResourceAllocationInfo(0, 1, &desc), block, offset);

				if (range)
				{
					if (state.Device10)
					{
						D3D12_RESOURCE_DESC1 desc1{};
						std::memcpy(&desc1, &desc, sizeof(desc));
						Check(state.Device10->CreatePlacedResource2(block, offset, &desc1, layout, nullptr, 0, nullptr, IID_PPV_ARGS(&resource)),
							what, &state);
					}
					else
					{
						Check(state.Device->CreatePlacedResource(block, offset, &desc, legacyState, nullptr, IID_PPV_ARGS(&resource)), what,
							&state);
					}

					placement = std::move(range);
					return resource;
				}
			}

			if (state.Device10)
			{
				D3D12_RESOURCE_DESC1 desc1{};
				std::memcpy(&desc1, &desc, sizeof(desc));
				Check(state.Device10->CreateCommittedResource3(
						  &heap, D3D12_HEAP_FLAG_NONE, &desc1, layout, nullptr, nullptr, 0, nullptr, IID_PPV_ARGS(&resource)),
					what, &state);
			}
			else
			{
				Check(state.Device->CreateCommittedResource(
						  &heap, D3D12_HEAP_FLAG_NONE, &desc, legacyState, nullptr, IID_PPV_ARGS(&resource)),
					what, &state);
			}

			return resource;
		}

		D3D12_TEXTURE_ADDRESS_MODE AddressMode(Rhi::SamplerAddressMode mode)
		{
			switch (mode)
			{
			case Rhi::SamplerAddressMode::MirroredRepeat:
				return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			case Rhi::SamplerAddressMode::ClampToEdge:
				return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			case Rhi::SamplerAddressMode::ClampToBorder:
				return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			default:
				return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			}
		}

	} // namespace

	D3D12_COMPARISON_FUNC ToComparison(Rhi::CompareOp op)
	{
		return static_cast<D3D12_COMPARISON_FUNC>(static_cast<int>(op) + 1); // Same order, D3D12 starts at 1.
	}

	// Buffer

	D3D12Buffer::D3D12Buffer(std::shared_ptr<D3D12DeviceState> stateValue, const Rhi::BufferDesc& descValue)
		: state(std::move(stateValue)), desc(descValue), name(descValue.DebugName)
	{
		desc.DebugName = name;

		if (desc.Size == 0)
		{
			throw std::invalid_argument("D3D12: buffers need a nonzero size");
		}

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = AlignUp64(desc.Size, 256); // Constant buffer views cover whole 256-byte blocks.
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		const auto heap = HeapType(desc.Memory);

		if (heap == D3D12_HEAP_TYPE_DEFAULT &&
			(Has(desc.Usage, Rhi::BufferUsage::Storage) || Has(desc.Usage, Rhi::BufferUsage::AccelerationStructureStorage)))
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		if (Has(desc.Usage, Rhi::BufferUsage::AccelerationStructureStorage))
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
		}

		TrackedState = heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
			: heap == D3D12_HEAP_TYPE_READBACK		  ? D3D12_RESOURCE_STATE_COPY_DEST
													  : D3D12_RESOURCE_STATE_COMMON;
		resource = CreateResource(
			*state, heap, resourceDesc, TrackedState, D3D12_BARRIER_LAYOUT_UNDEFINED, "Create buffer resource", placement);
		memory = D3D12MemoryRecord(*state, resourceDesc, heap);
		SetDebugName(resource.Get(), name);

		if (heap != D3D12_HEAP_TYPE_DEFAULT)
		{
			// Host heaps stay mapped for the buffer's life (upload is write-combined, readback cached).
			void* pointer = nullptr;
			const D3D12_RANGE none{ 0, 0 };
			Check(resource->Map(0, heap == D3D12_HEAP_TYPE_READBACK ? nullptr : &none, &pointer), "Map (buffer)", state.get());
			mapped = static_cast<std::byte*>(pointer);
		}
	}

	D3D12Buffer::~D3D12Buffer()
	{
		if (mapped)
		{
			resource->Unmap(0, nullptr);
		}

		state->Deferred->Release(std::move(resource), std::move(placement), std::move(memory));
	}

	void D3D12Buffer::Write(std::uint64_t offset, std::span<const std::byte> data)
	{
		if (desc.Memory != Rhi::MemoryPreference::CpuToGpu || !mapped || offset > desc.Size || data.size() > desc.Size - offset)
		{
			throw std::invalid_argument("D3D12: Buffer::Write needs a CpuToGpu buffer and an in-range write");
		}

		std::memcpy(mapped + offset, data.data(), data.size());
	}

	void D3D12Buffer::Read(std::uint64_t offset, std::span<std::byte> data)
	{
		if (desc.Memory != Rhi::MemoryPreference::GpuToCpu || !mapped || offset > desc.Size || data.size() > desc.Size - offset)
		{
			throw std::invalid_argument("D3D12: Buffer::Read needs a GpuToCpu buffer and an in-range read");
		}

		std::memcpy(data.data(), mapped + offset, data.size());
	}

	std::span<std::byte> D3D12Buffer::GetMappedWriteSpan()
	{
		return desc.PersistentMap && desc.Memory == Rhi::MemoryPreference::CpuToGpu && mapped ? std::span<std::byte>(mapped, desc.Size)
																							  : std::span<std::byte>{};
	}

	void D3D12Buffer::FlushMappedWrites(std::uint64_t offset, std::uint64_t size)
	{
		if (GetMappedWriteSpan().empty() || offset > desc.Size || size > desc.Size - offset)
		{
			throw std::logic_error("D3D12: FlushMappedWrites needs a persistently mapped CpuToGpu buffer and an in-range span");
		}
	}

	std::span<const std::byte> D3D12Buffer::GetMappedReadSpan()
	{
		return desc.PersistentMap && desc.Memory == Rhi::MemoryPreference::GpuToCpu && mapped
			? std::span<const std::byte>(mapped, desc.Size)
			: std::span<const std::byte>{};
	}

	void D3D12Buffer::InvalidateMappedReads(std::uint64_t offset, std::uint64_t size)
	{
		if (GetMappedReadSpan().empty() || offset > desc.Size || size > desc.Size - offset)
		{
			throw std::logic_error("D3D12: InvalidateMappedReads needs a persistently mapped GpuToCpu buffer and an in-range span");
		}
	}

	// Texture

	D3D12Texture::D3D12Texture(std::shared_ptr<D3D12DeviceState> stateValue, const Rhi::TextureDesc& descValue)
		: state(std::move(stateValue)), desc(descValue), name(descValue.DebugName)
	{
		desc.DebugName = name;
		const auto format = ToTypelessFormat(desc.PixelFormat);

		if (format == DXGI_FORMAT_UNKNOWN || desc.Extent.Width == 0 || desc.Extent.Height == 0 || desc.MipLevels == 0 ||
			desc.ArrayLayers == 0)
		{
			throw std::invalid_argument("D3D12: unsupported texture format or empty texture");
		}

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = desc.Dimension == Rhi::TextureDimension::Texture1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
			: desc.Dimension == Rhi::TextureDimension::Texture3D					? D3D12_RESOURCE_DIMENSION_TEXTURE3D
																					: D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = desc.Extent.Width;
		resourceDesc.Height = desc.Extent.Height;
		resourceDesc.DepthOrArraySize =
			static_cast<UINT16>(desc.Dimension == Rhi::TextureDimension::Texture3D ? desc.Extent.Depth : desc.ArrayLayers);
		resourceDesc.MipLevels = static_cast<UINT16>(desc.MipLevels);
		resourceDesc.Format = format;
		resourceDesc.SampleDesc.Count = static_cast<UINT>(desc.Samples);

		if (Has(desc.Usage, Rhi::TextureUsage::ColorAttachment))
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}

		if (Has(desc.Usage, Rhi::TextureUsage::DepthStencilAttachment))
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}

		if (Has(desc.Usage, Rhi::TextureUsage::Storage))
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		resource = CreateResource(*state, D3D12_HEAP_TYPE_DEFAULT, resourceDesc, D3D12_RESOURCE_STATE_COMMON, D3D12_BARRIER_LAYOUT_COMMON,
			"Create texture resource", placement);
		// Created in the COMMON layout; with no attachment or storage use it never leaves it.
		commonLayout = state->Features.EnhancedBarriers && resourceDesc.Flags == D3D12_RESOURCE_FLAG_NONE;
		memory = D3D12MemoryRecord(*state, resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
		SetDebugName(resource.Get(), name);
		TrackedStates.assign(GetSubresourceCount(), D3D12_RESOURCE_STATE_COMMON);
	}

	D3D12Texture::~D3D12Texture()
	{
		// A wrapped (swapchain) texture is only a reference: dropped now, so the swapchain can resize.
		if (memory.Owned())
		{
			state->Deferred->Release(std::move(resource), std::move(placement), std::move(memory));
		}
	}

	D3D12Texture::D3D12Texture(
		std::shared_ptr<D3D12DeviceState> stateValue, ComPtr<ID3D12Resource> resourceValue, const Rhi::TextureDesc& descValue)
		: state(std::move(stateValue)), desc(descValue), resource(std::move(resourceValue))
	{
		desc.DebugName = {};
		TrackedStates.assign(GetSubresourceCount(), D3D12_RESOURCE_STATE_PRESENT);
	}

	std::uint32_t D3D12Texture::GetSubresourceCount() const
	{
		const auto layers = desc.Dimension == Rhi::TextureDimension::Texture3D ? 1u : desc.ArrayLayers;
		return desc.MipLevels * layers * GetPlaneCount();
	}

	std::uint32_t D3D12Texture::Subresource(std::uint32_t mip, std::uint32_t layer, std::uint32_t plane) const
	{
		const auto layers = desc.Dimension == Rhi::TextureDimension::Texture3D ? 1u : desc.ArrayLayers;
		return mip + layer * desc.MipLevels + plane * desc.MipLevels * layers;
	}

	// Sampler

	D3D12Sampler::D3D12Sampler(std::shared_ptr<D3D12DeviceState> stateValue, const Rhi::SamplerDesc& descValue)
		: state(std::move(stateValue)), desc(descValue)
	{
		desc.DebugName = {};
		D3D12_SAMPLER_DESC sampler{};
		const bool point = desc.MinFilter == Rhi::Filter::Nearest;
		const bool magPoint = desc.MagFilter == Rhi::Filter::Nearest;
		const bool mipPoint = desc.MipFilter == Rhi::Filter::Nearest;
		const int filter = (point ? 0 : 0x10) | (magPoint ? 0 : 0x4) | (mipPoint ? 0 : 0x1);
		sampler.Filter = desc.EnableAnisotropy ? D3D12_FILTER_ANISOTROPIC : static_cast<D3D12_FILTER>(filter);

		if (desc.EnableComparison)
		{
			sampler.Filter = static_cast<D3D12_FILTER>(sampler.Filter | 0x80);
		}

		sampler.AddressU = AddressMode(desc.AddressU);
		sampler.AddressV = AddressMode(desc.AddressV);
		sampler.AddressW = AddressMode(desc.AddressW);
		sampler.MipLODBias = desc.MipLodBias;
		sampler.MaxAnisotropy = desc.EnableAnisotropy ? std::clamp(static_cast<UINT>(desc.MaxAnisotropy), 1u, 16u) : 1u;
		sampler.ComparisonFunc = desc.EnableComparison ? ToComparison(desc.Comparison) : D3D12_COMPARISON_FUNC_NEVER;
		sampler.MinLOD = desc.MinLod;
		sampler.MaxLOD = desc.MaxLod;
		slot = state->CpuSamplers->Allocate();
		state->Device->CreateSampler(&sampler, state->CpuSamplers->Cpu(slot));
	}

	D3D12Sampler::~D3D12Sampler()
	{
		state->CpuSamplers->Free(slot);
	}

} // namespace Swim::RhiD3D12
