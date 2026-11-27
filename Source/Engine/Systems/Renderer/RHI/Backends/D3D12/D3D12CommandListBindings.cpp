#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12CommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Pipelines.h"

#include <cstring>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	// Query pool

	D3D12QueryPool::D3D12QueryPool(
		std::shared_ptr<D3D12DeviceState> stateValue, const Rhi::QueryPoolDesc& descValue, Rhi::TimestampInfo infoValue)
		: state(std::move(stateValue)), desc(descValue), name(descValue.DebugName), info(infoValue), written(descValue.Count, false)
	{
		this->desc.DebugName = name;

		if (desc.Type != Rhi::QueryType::Timestamp || desc.Count == 0)
		{
			throw std::invalid_argument("D3D12: query pools hold timestamps");
		}

		const bool copy = desc.Queue == Rhi::QueueType::Transfer;
		type = D3D12_QUERY_TYPE_TIMESTAMP;
		D3D12_QUERY_HEAP_DESC heapDesc{ copy ? D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_TIMESTAMP, desc.Count,
			0 };
		Check(state->Device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&heap)), "CreateQueryHeap", state.get());
		SetDebugName(heap.Get(), name);
		const D3D12_HEAP_PROPERTIES readbackHeap{ D3D12_HEAP_TYPE_READBACK };
		D3D12_RESOURCE_DESC buffer{};
		buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer.Width = std::uint64_t(desc.Count) * sizeof(std::uint64_t);
		buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
		buffer.SampleDesc.Count = 1;
		buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		Check(state->Device->CreateCommittedResource(
				  &readbackHeap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
			"CreateCommittedResource (timestamps)", state.get());
		SetDebugName(readback.Get(), name);
	}

	void D3D12QueryPool::MarkReset(std::uint32_t first, std::uint32_t count)
	{
		std::scoped_lock lock(mutex);

		for (std::uint32_t index = first; index < first + count && index < written.size(); ++index)
		{
			written[index] = false;
		}
	}

	void D3D12QueryPool::MarkWritten(std::uint32_t index)
	{
		std::scoped_lock lock(mutex);
		written.at(index) = true;
	}

	Rhi::QueryReadStatus D3D12QueryPool::ReadTimestamps(std::uint32_t first, std::span<Rhi::TimestampResult> results)
	{
		std::fill(results.begin(), results.end(), Rhi::TimestampResult{});

		if (first > desc.Count || results.size() > desc.Count - first)
		{
			return Rhi::QueryReadStatus::Error;
		}

		const D3D12_RANGE range{ first * sizeof(std::uint64_t), (first + results.size()) * sizeof(std::uint64_t) };
		void* mapped = nullptr;

		if (FAILED(readback->Map(0, &range, &mapped)))
		{
			return Rhi::QueryReadStatus::Error;
		}

		const auto* ticks = static_cast<const std::uint64_t*>(mapped);
		bool ready = true;
		std::scoped_lock lock(mutex);

		for (std::size_t index = 0; index < results.size(); ++index)
		{
			if (written[first + index])
			{
				results[index] = { ticks[first + index], true };
			}
			else
			{
				ready = false;
			}
		}

		const D3D12_RANGE none{ 0, 0 };
		readback->Unmap(0, &none);
		return ready ? Rhi::QueryReadStatus::Ready : Rhi::QueryReadStatus::NotReady;
	}

	// Command list: pipelines, root arguments, queries

	void D3D12CommandList::BindGraphicsPipeline(Rhi::GraphicsPipeline& pipeline)
	{
		auto& native = static_cast<D3D12GraphicsPipeline&>(pipeline);

		if (boundPipeline != native.GetPipeline())
		{
			boundPipeline = native.GetPipeline();
			list->SetPipelineState(boundPipeline);
		}

		if (graphicsLayout != &native.GetLayout())
		{
			graphicsLayout = &native.GetLayout();
			list->SetGraphicsRootSignature(graphicsLayout->GetRootSignature());
			graphicsApplied.clear();
			graphicsPushDirty = true;
			graphicsDirty = true;
		}

		if (boundTopology != native.GetTopology())
		{
			boundTopology = native.GetTopology();
			list->IASetPrimitiveTopology(boundTopology);
		}

		if (boundStencilReference != native.GetStencilReference())
		{
			boundStencilReference = native.GetStencilReference();
			list->OMSetStencilRef(boundStencilReference);
		}

		graphicsPipeline = &native;
		computeActive = false;
		graphicsDirty = true;

		// Vertex buffers bound before the pipeline take its strides now.
		for (std::uint32_t slot = 0; slot < vertexBuffers.size(); ++slot)
		{
			if (vertexBuffers[slot].BufferLocation != 0 && vertexBuffers[slot].StrideInBytes != native.GetStride(slot))
			{
				vertexBuffers[slot].StrideInBytes = native.GetStride(slot);
				list->IASetVertexBuffers(slot, 1, &vertexBuffers[slot]);
			}
		}
	}

	void D3D12CommandList::BindComputePipeline(Rhi::ComputePipeline& pipeline)
	{
		auto& native = static_cast<D3D12ComputePipeline&>(pipeline);

		if (boundPipeline != native.GetPipeline())
		{
			boundPipeline = native.GetPipeline();
			list->SetPipelineState(boundPipeline);
		}

		if (computeLayout != &native.GetLayout())
		{
			computeLayout = &native.GetLayout();
			list->SetComputeRootSignature(computeLayout->GetRootSignature());
			computeApplied.clear();
			computePushDirty = true;
			computeDirty = true;
		}

		computeActive = true;
		computeDirty = true;
	}

	void D3D12CommandList::BindDescriptorTable(std::uint32_t space, Rhi::DescriptorTable& table)
	{
		auto* layout = computeActive ? computeLayout : graphicsLayout;

		if (!layout)
		{
			throw std::logic_error("Bind a pipeline before descriptors or push constants");
		}

		auto& native = static_cast<D3D12DescriptorTable&>(table);

		if (space != native.GetSpace() || !native.IsComplete())
		{
			throw std::invalid_argument(
				"Descriptor table must match an identically defined pipeline layout space and have every required element initialized");
		}

		(computeActive ? computeTables : graphicsTables).Bind(space, &native, native.GetLayout().GetInterface(), layout->GetInterface());
		(computeActive ? computeDirty : graphicsDirty) = true;
	}

	void D3D12CommandList::PushConstants(Rhi::ShaderStageMask, std::uint32_t offset, std::span<const std::byte> data)
	{
		if (offset % 4 != 0 || data.size() % 4 != 0 || offset + data.size() > sizeof(pushConstants))
		{
			throw std::invalid_argument("D3D12: push constants must be four-byte aligned and fit 128 bytes");
		}

		auto* target = reinterpret_cast<std::byte*>(pushConstants.data()) + offset;

		if (std::memcmp(target, data.data(), data.size()) == 0)
		{
			return;
		}

		std::memcpy(target, data.data(), data.size());
		graphicsPushDirty = computePushDirty = true;
		graphicsDirty = computeDirty = true;
	}

	void D3D12CommandList::ApplyRootBindings(bool compute)
	{
		auto* layout = compute ? computeLayout : graphicsLayout;
		bool& dirty = compute ? computeDirty : graphicsDirty;
		bool& pushDirty = compute ? computePushDirty : graphicsPushDirty;
		auto& applied = compute ? computeApplied : graphicsApplied;

		if (!layout)
		{
			throw std::logic_error(compute ? "D3D12: dispatch without a compute pipeline" : "D3D12: draw without a graphics pipeline");
		}

		if (!dirty)
		{
			return;
		}

		const auto& bound = compute ? computeTables : graphicsTables;
		bound.Require(layout->GetInterface());
		dirty = false;
		const auto setTable = [&](int parameter, D3D12_GPU_DESCRIPTOR_HANDLE handle)
		{
			if (parameter < 0 || handle.ptr == 0)
			{
				return;
			}

			compute ? list->SetComputeRootDescriptorTable(parameter, handle) : list->SetGraphicsRootDescriptorTable(parameter, handle);
		};

		for (const auto& space : layout->GetSpaces())
		{
			if (const auto* found = static_cast<const D3D12DescriptorTable*>(bound.Get(space.Index, layout->GetInterface())))
			{
				const auto& table = *found;

				if (applied.size() <= space.Index)
				{
					applied.resize(space.Index + 1);
				}

				// The same table with no writes since it was applied: its arguments are set.
				auto& last = applied[space.Index];

				if (last.Table == &table && last.Version == table.GetVersion())
				{
					continue;
				}

				last = { &table, table.GetVersion() };
				setTable(space.ViewParameter, table.GetViews());
				setTable(space.SamplerParameter, table.GetSamplers());

				for (const auto& slot : space.Slots)
				{
					const auto address = slot.RootParameter < 0 ? 0 : table.GetBufferAddress(slot.Binding.Binding);

					if (address == 0)
					{
						continue;
					}

					const auto parameter = static_cast<UINT>(slot.RootParameter);
					const bool write = slot.Binding.Type == Rhi::DescriptorType::StorageBuffer;

					if (compute)
					{
						write ? list->SetComputeRootUnorderedAccessView(parameter, address)
							  : list->SetComputeRootShaderResourceView(parameter, address);
					}
					else
					{
						write ? list->SetGraphicsRootUnorderedAccessView(parameter, address)
							  : list->SetGraphicsRootShaderResourceView(parameter, address);
					}
				}
			}
		}

		if (pushDirty && layout->GetPushParameter() >= 0)
		{
			const auto parameter = static_cast<UINT>(layout->GetPushParameter());
			compute ? list->SetComputeRoot32BitConstants(parameter, layout->GetPushDwords(), pushConstants.data(), 0)
					: list->SetGraphicsRoot32BitConstants(parameter, layout->GetPushDwords(), pushConstants.data(), 0);
		}

		pushDirty = false;
	}

	void D3D12CommandList::PrepareDraw(std::uint32_t baseVertex, std::uint32_t baseInstance)
	{
		if (!rendering)
		{
			throw std::logic_error("D3D12: draw outside rendering");
		}

		ApplyRootBindings(false);

		if (graphicsLayout->GetDrawBaseParameter() >= 0)
		{
			const std::uint32_t base[2] = { baseVertex, baseInstance };
			list->SetGraphicsRoot32BitConstants(static_cast<UINT>(graphicsLayout->GetDrawBaseParameter()), 2, base, 0);
		}
	}

	std::uint32_t D3D12CommandList::VertexStride(std::uint32_t slot) const
	{
		return graphicsPipeline ? graphicsPipeline->GetStride(slot) : 0u;
	}

	ID3D12CommandSignature* D3D12CommandList::GetIndirectSignature(int kind, std::uint32_t stride)
	{
		return state->GetCommandSignature(kind, stride);
	}

	void D3D12CommandList::ResetQueries(Rhi::QueryPool& pool, std::uint32_t first, std::uint32_t count)
	{
		static_cast<D3D12QueryPool&>(pool).MarkReset(first, count);
	}

	void D3D12CommandList::WriteTimestamp(Rhi::QueryPool& pool, std::uint32_t index, Rhi::TimestampStage)
	{
		FlushCopies();
		auto& native = static_cast<D3D12QueryPool&>(pool);
		list->EndQuery(native.GetHeap(), native.GetQueryType(), index);
		native.MarkWritten(index);
		pendingTimestamps.emplace_back(&native, index);
	}

	void D3D12CommandList::ResolveTimestamps()
	{
		for (const auto& [pool, index] : pendingTimestamps)
		{
			list->ResolveQueryData(
				pool->GetHeap(), pool->GetQueryType(), index, 1, pool->GetReadback(), std::uint64_t(index) * sizeof(std::uint64_t));
		}

		pendingTimestamps.clear();
	}

} // namespace Swim::RhiD3D12
