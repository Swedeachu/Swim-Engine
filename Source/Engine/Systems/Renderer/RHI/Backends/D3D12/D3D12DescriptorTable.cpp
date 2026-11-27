#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Pipelines.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Swim::RhiD3D12
{

	namespace
	{

		D3D12_SRV_DIMENSION ToSrvDimension(Rhi::TextureViewDimension dimension)
		{
			switch (dimension)
			{
			case Rhi::TextureViewDimension::Texture2DArray:
				return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			case Rhi::TextureViewDimension::TextureCube:
				return D3D12_SRV_DIMENSION_TEXTURECUBE;
			case Rhi::TextureViewDimension::TextureCubeArray:
				return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			case Rhi::TextureViewDimension::Texture3D:
				return D3D12_SRV_DIMENSION_TEXTURE3D;
			default:
				return D3D12_SRV_DIMENSION_TEXTURE2D;
			}
		}

	} // namespace

	std::unique_ptr<D3D12DescriptorTable> D3D12DescriptorTable::Create(
		std::shared_ptr<D3D12DeviceState> state, const Rhi::DescriptorTableDesc& desc)
	{
		auto* layout = dynamic_cast<D3D12PipelineLayout*>(desc.Layout);
		const auto* space = layout ? layout->FindSpace(desc.Space) : nullptr;

		if (!space)
		{
			return nullptr;
		}

		auto table = std::unique_ptr<D3D12DescriptorTable>(new D3D12DescriptorTable(std::move(state), *layout, *space));
		auto& self = *table;

		// A variable-count binding (the last of its kind) holds only the requested descriptors.
		for (auto& slot : self.space.Slots)
		{
			if (slot.Binding.VariableCount)
			{
				const auto requested = std::min(desc.VariableDescriptorCount, slot.Binding.Count);
				(slot.Sampler ? self.space.SamplerCount : self.space.ViewCount) -= slot.Binding.Count - requested;
				slot.Binding.Count = requested;
			}
		}

		self.viewCount = self.space.ViewCount;
		self.samplerCount = self.space.SamplerCount;

		if (self.viewCount)
		{
			self.views = self.state->Views->Allocate(self.viewCount);
		}

		if (self.samplerCount)
		{
			self.samplers = self.state->Samplers->Allocate(self.samplerCount);
		}

		if ((self.viewCount && self.views == D3D12DescriptorHeap::Invalid) ||
			(self.samplerCount && self.samplers == D3D12DescriptorHeap::Invalid))
		{
			throw std::runtime_error("D3D12: the shader-visible descriptor heap is full");
		}

		// A table binds once every element is written, except those of partially bound
		// bindings (bindless arrays). Those start as null descriptors: a shader index past what
		// was written then reads zeros instead of an uninitialised descriptor, which D3D12
		// leaves undefined (it can hang the GPU).
		for (const auto& slot : self.space.Slots)
		{
			self.written.emplace_back(slot.Binding.Count, false);

			if (slot.Binding.PartiallyBound)
			{
				for (std::uint32_t element = 0; element < slot.Binding.Count; ++element)
				{
					self.WriteNull(slot, element);
				}
			}
		}

		return table;
	}

	D3D12DescriptorTable::D3D12DescriptorTable(
		std::shared_ptr<D3D12DeviceState> stateValue, D3D12PipelineLayout& layoutValue, const D3D12PipelineLayout::Space& spaceValue)
		: state(std::move(stateValue)), layout(layoutValue), space(spaceValue)
	{
	}

	D3D12DescriptorTable::~D3D12DescriptorTable()
	{
		if (views != D3D12DescriptorHeap::Invalid)
		{
			state->Views->Free(views, viewCount);
		}

		if (samplers != D3D12DescriptorHeap::Invalid)
		{
			state->Samplers->Free(samplers, samplerCount);
		}
	}

	D3D12_GPU_VIRTUAL_ADDRESS D3D12DescriptorTable::GetBufferAddress(std::uint32_t binding) const
	{
		const auto found = bufferAddresses.find(binding);
		return found == bufferAddresses.end() ? 0 : found->second;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorTable::GetViews() const
	{
		return views == D3D12DescriptorHeap::Invalid ? D3D12_GPU_DESCRIPTOR_HANDLE{} : state->Views->Gpu(views);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorTable::GetSamplers() const
	{
		return samplers == D3D12DescriptorHeap::Invalid ? D3D12_GPU_DESCRIPTOR_HANDLE{} : state->Samplers->Gpu(samplers);
	}

	void D3D12DescriptorTable::WriteNull(const D3D12PipelineLayout::Slot& slot, std::uint32_t element)
	{
		auto* device = state->Device.Get();

		if (slot.Sampler)
		{
			D3D12_SAMPLER_DESC sampler{};
			sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			device->CreateSampler(&sampler, state->Samplers->Cpu(samplers + slot.Offset + element));
			return;
		}

		const auto destination = state->Views->Cpu(views + slot.Offset + element);
		const auto stride = slot.Native ? slot.Native->StructureStride : 0u;

		switch (slot.Binding.Type)
		{
		case Rhi::DescriptorType::UniformBuffer:
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
			device->CreateConstantBufferView(&cbv, destination);
			break;
		}
		case Rhi::DescriptorType::StorageBuffer:
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uav.Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
			uav.Buffer.StructureByteStride = stride;
			uav.Buffer.Flags = stride ? D3D12_BUFFER_UAV_FLAG_NONE : D3D12_BUFFER_UAV_FLAG_RAW;
			device->CreateUnorderedAccessView(nullptr, nullptr, &uav, destination);
			break;
		}
		case Rhi::DescriptorType::StorageTexture:
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uav.Format = ToDxgiFormat(slot.Binding.StorageTextureFormat);
			device->CreateUnorderedAccessView(nullptr, nullptr, &uav, destination);
			break;
		}
		case Rhi::DescriptorType::AccelerationStructure:
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			device->CreateShaderResourceView(nullptr, &srv, destination);
			break;
		}
		case Rhi::DescriptorType::ReadOnlyStorageBuffer:
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srv.Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
			srv.Buffer.StructureByteStride = stride;
			srv.Buffer.Flags = stride ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW;
			device->CreateShaderResourceView(nullptr, &srv, destination);
			break;
		}
		default:
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.ViewDimension =
				slot.Binding.Multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : ToSrvDimension(slot.Binding.SampledDimension);
			srv.Format = slot.Binding.SampledClass == Rhi::SampledTextureClass::Uint ? DXGI_FORMAT_R32_UINT
				: slot.Binding.SampledClass == Rhi::SampledTextureClass::Sint		 ? DXGI_FORMAT_R32_SINT
																					 : DXGI_FORMAT_R8G8B8A8_UNORM;
			device->CreateShaderResourceView(nullptr, &srv, destination);
			break;
		}
		}
	}

	bool D3D12DescriptorTable::IsComplete() const
	{
		for (std::size_t index = 0; index < written.size(); ++index)
		{
			if (!space.Slots[index].Binding.PartiallyBound &&
				std::find(written[index].begin(), written[index].end(), false) != written[index].end())
			{
				return false;
			}
		}

		return true;
	}

	void D3D12DescriptorTable::Write(std::span<const Rhi::DescriptorWrite> writes)
	{
		auto* device = state->Device.Get();
		++version;

		for (const auto& write : writes)
		{
			const auto slot = std::find_if(space.Slots.begin(), space.Slots.end(),
				[&](const D3D12PipelineLayout::Slot& candidate)
				{
					return candidate.Binding.Binding == write.Binding;
				});

			if (slot == space.Slots.end() || write.ArrayIndex >= slot->Binding.Count)
			{
				throw std::invalid_argument("D3D12: descriptor write outside the table");
			}

			const auto marked = [this, slot, element = write.ArrayIndex]
			{
				written[std::size_t(slot - space.Slots.begin())][element] = true;
			};

			if (slot->Sampler)
			{
				if (!write.SamplerResource)
				{
					throw std::invalid_argument("D3D12: sampler write without a sampler");
				}

				device->CopyDescriptorsSimple(1, state->Samplers->Cpu(samplers + slot->Offset + write.ArrayIndex),
					static_cast<D3D12Sampler*>(write.SamplerResource)->GetCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				marked();
				continue;
			}

			const auto destination = state->Views->Cpu(views + slot->Offset + write.ArrayIndex);
			const auto type = slot->Binding.Type;

			if (type == Rhi::DescriptorType::SampledTexture || type == Rhi::DescriptorType::StorageTexture)
			{
				auto* view = static_cast<D3D12TextureView*>(write.TextureResource);

				if (!view)
				{
					throw std::invalid_argument("D3D12: texture write without a view");
				}

				type == Rhi::DescriptorType::SampledTexture ? view->WriteShaderResource(destination)
															: view->WriteUnorderedAccess(destination);
				marked();
				continue;
			}

			auto* buffer = static_cast<D3D12Buffer*>(write.BufferResource);

			if (!buffer)
			{
				throw std::invalid_argument("D3D12: buffer write without a buffer");
			}

			const auto size = buffer->GetDesc().Size;
			const auto range = write.BufferRange ? write.BufferRange : size - write.BufferOffset;
			const auto stride = slot->Native ? slot->Native->StructureStride : 0u;

			if (slot->Binding.Count == 1)
			{
				bufferAddresses[write.Binding] = buffer->GetAddress() + write.BufferOffset;
			}

			if (type == Rhi::DescriptorType::UniformBuffer)
			{
				const auto bytes = std::min<std::uint64_t>(
					AlignUp64(range, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u);
				const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{ buffer->GetAddress() + write.BufferOffset, static_cast<UINT>(bytes) };
				device->CreateConstantBufferView(&cbv, destination);
			}
			else if (type == Rhi::DescriptorType::AccelerationStructure)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
				srv.RaytracingAccelerationStructure.Location = buffer->GetAddress() + write.BufferOffset;
				device->CreateShaderResourceView(nullptr, &srv, destination);
			}
			else if (stride && slot->RootParameter < 0 && write.BufferOffset % stride != 0)
			{
				// A structured view in a table starts at a whole element (D3D12_BUFFER_SRV and
				// D3D12_BUFFER_UAV: FirstElement is an element index), so this offset cannot be
				// expressed and rounding it would read shifted records. Reported once per binding;
				// the binding reads as a null view (zeros) instead.
				const auto& native = *slot->Native;
				const char registerClass = type == Rhi::DescriptorType::StorageBuffer ? 'u' : 't';
				std::string text = "structured-buffer view at space " + std::to_string(native.Space) + " binding " +
					std::to_string(native.Binding) + " (register " + registerClass + std::to_string(native.Register) + ", space" +
					std::to_string(native.RegisterSpace) + ", buffer \"" + std::string(buffer->GetDesc().DebugName) + "\"): offset " +
					std::to_string(write.BufferOffset) + " is not a multiple of the structure stride " + std::to_string(stride) +
					"; bound as a null view";
				std::string key = "structured-offset " + std::to_string(native.Space) + "/" + std::to_string(native.Binding) + "/" +
					std::to_string(native.Register) + "/" + std::to_string(native.RegisterSpace);
				state->ReportOnce(std::move(key), "D3D12-RHI-STRUCTURED-OFFSET", text);
				WriteNull(*slot, write.ArrayIndex);
			}
			else if (type == Rhi::DescriptorType::StorageBuffer)
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				uav.Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
				uav.Buffer.FirstElement = write.BufferOffset / (stride ? stride : 4u);
				uav.Buffer.NumElements = static_cast<UINT>(range / (stride ? stride : 4u));
				uav.Buffer.StructureByteStride = stride;
				uav.Buffer.Flags = stride ? D3D12_BUFFER_UAV_FLAG_NONE : D3D12_BUFFER_UAV_FLAG_RAW;
				device->CreateUnorderedAccessView(buffer->GetResource(), nullptr, &uav, destination);
			}
			else
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srv.Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
				srv.Buffer.FirstElement = write.BufferOffset / (stride ? stride : 4u);
				srv.Buffer.NumElements = static_cast<UINT>(range / (stride ? stride : 4u));
				srv.Buffer.StructureByteStride = stride;
				srv.Buffer.Flags = stride ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW;
				device->CreateShaderResourceView(buffer->GetResource(), &srv, destination);
			}

			marked();
		}
	}

} // namespace Swim::RhiD3D12
