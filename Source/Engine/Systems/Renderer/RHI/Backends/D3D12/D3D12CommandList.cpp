#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12CommandList.h"

#include <stdexcept>

namespace Swim::RhiD3D12
{

	D3D12_COMMAND_LIST_TYPE ToCommandListType(Rhi::QueueType type)
	{
		switch (type)
		{
		case Rhi::QueueType::Compute:
			return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case Rhi::QueueType::Transfer:
			return D3D12_COMMAND_LIST_TYPE_COPY;
		default:
			return D3D12_COMMAND_LIST_TYPE_DIRECT;
		}
	}

	// Pool

	D3D12CommandPool::D3D12CommandPool(std::shared_ptr<D3D12DeviceState> stateValue, Rhi::QueueType typeValue)
		: state(std::move(stateValue)), type(typeValue), allocators(std::make_shared<D3D12AllocatorSet>())
	{
	}

	std::unique_ptr<Rhi::CommandList> D3D12CommandPool::CreateCommandList()
	{
		return std::make_unique<D3D12CommandList>(state, type, allocators);
	}

	void D3D12CommandPool::Reset()
	{
		std::scoped_lock lock(allocators->Mutex);

		for (const auto& allocator : allocators->All)
		{
			Check(allocator->Reset(), "ID3D12CommandAllocator::Reset", state.get());
		}

		for (auto& block : allocators->RetiredStaging)
		{
			allocators->FreeStaging.push_back(std::move(block));
		}

		allocators->RetiredStaging.clear();
	}

	// List

	D3D12CommandList::D3D12CommandList(
		std::shared_ptr<D3D12DeviceState> stateValue, Rhi::QueueType typeValue, std::shared_ptr<D3D12AllocatorSet> set)
		: state(std::move(stateValue)), type(typeValue), allocators(std::move(set))
	{
		const auto listType = ToCommandListType(type);

		{
			std::scoped_lock lock(allocators->Mutex);

			if (!allocators->Free.empty())
			{
				allocator = allocators->Free.back();
				allocators->Free.pop_back();
			}
			else
			{
				ComPtr<ID3D12CommandAllocator> created;
				Check(state->Device->CreateCommandAllocator(listType, IID_PPV_ARGS(&created)), "CreateCommandAllocator", state.get());
				allocator = created.Get();
				allocators->All.push_back(std::move(created));
			}

			if (!allocators->FreeLists.empty())
			{
				list = std::move(allocators->FreeLists.back());
				allocators->FreeLists.pop_back();
			}
		}

		if (!list)
		{
			// Created closed; Begin resets it onto the allocator.
			Check(state->Device->CreateCommandList1(0, listType, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&list)),
				"CreateCommandList1", state.get());
			SetDebugName(list.Get(),
				type == Rhi::QueueType::Graphics	  ? "Swim direct list"
					: type == Rhi::QueueType::Compute ? "Swim compute list"
													  : "Swim copy list");
		}

		if (state->Features.EnhancedBarriers)
		{
			list.As(&list7);
		}
	}

	D3D12CommandList::~D3D12CommandList()
	{
		RetireStaging();
		// A list dropped while recording is closed first; only a closed list can be reset.
		const bool reusable = !recording || SUCCEEDED(list->Close());
		std::scoped_lock lock(allocators->Mutex);
		allocators->Free.push_back(allocator);

		if (reusable)
		{
			allocators->FreeLists.push_back(std::move(list));
		}
	}

	void D3D12CommandList::Begin()
	{
		if (recording)
		{
			throw std::logic_error("D3D12: Begin on a recording command list");
		}

		Check(list->Reset(allocator, nullptr), "ID3D12GraphicsCommandList::Reset", state.get());
		recording = true;
		rendering = false;
		graphicsLayout = nullptr;
		computeLayout = nullptr;
		graphicsPipeline = nullptr;
		graphicsDirty = computeDirty = false;
		graphicsApplied.clear();
		computeApplied.clear();
		graphicsPushDirty = computePushDirty = false;
		boundPipeline = nullptr;
		boundTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		boundStencilReference = ~0u;
		vertexBuffers = {};
		graphicsTables.Reset();
		computeTables.Reset();
		computeActive = false;
		scratch.clear();
		RetireStaging();
		pendingTimestamps.clear();

		if (type != Rhi::QueueType::Transfer)
		{
			ID3D12DescriptorHeap* heaps[] = { state->Views->Get(), state->Samplers->Get() };
			list->SetDescriptorHeaps(2, heaps);
		}
	}

	void D3D12CommandList::End()
	{
		if (!recording || rendering)
		{
			throw std::logic_error("D3D12: End needs a recording command list outside rendering");
		}

		FlushCopies();
		ResolveTimestamps();
		const auto closed = list->Close();
		state->DrainMessages(); // Polled messages say why a Close failed.
		Check(closed, "ID3D12GraphicsCommandList::Close", state.get());
		recording = false;
	}

	void D3D12CommandList::BeginDebugLabel(std::string_view name, const std::array<float, 4>&)
	{
		FlushCopies();
		const auto wide = Widen(name);
		list->BeginEvent(0, wide.c_str(), static_cast<UINT>((wide.size() + 1) * sizeof(wchar_t)));
	}

	void D3D12CommandList::EndDebugLabel()
	{
		FlushCopies();
		list->EndEvent();
	}

	void D3D12CommandList::InsertDebugLabel(std::string_view name, const std::array<float, 4>&)
	{
		const auto wide = Widen(name);
		list->SetMarker(0, wide.c_str(), static_cast<UINT>((wide.size() + 1) * sizeof(wchar_t)));
	}

	void D3D12CommandList::BeginRendering(const Rhi::RenderingDesc& desc)
	{
		if (rendering)
		{
			throw std::logic_error("D3D12: BeginRendering inside rendering");
		}

		FlushCopies();
		std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
		const auto count = std::min<std::size_t>(desc.ColorAttachments.size(), targets.size());
		const D3D12_RECT area{ 0, 0, static_cast<LONG>(desc.RenderArea.Width), static_cast<LONG>(desc.RenderArea.Height) };

		for (std::size_t index = 0; index < count; ++index)
		{
			const auto& attachment = desc.ColorAttachments[index];
			targets[index] = static_cast<D3D12TextureView&>(*attachment.View).GetRenderTarget();

			if (attachment.Load == Rhi::LoadOp::Clear)
			{
				list->ClearRenderTargetView(targets[index], attachment.Clear.Value.data(), 1, &area);
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE depth{};
		const auto* depthDesc = desc.DepthStencilAttachment;

		if (depthDesc && depthDesc->View)
		{
			auto& view = static_cast<D3D12TextureView&>(*depthDesc->View);
			const auto last = view.GetD3D12Texture().LastState;
			const bool readOnly =
				Rhi::HasAny(last, Rhi::ResourceState::DepthStencilRead) && !Rhi::HasAny(last, Rhi::ResourceState::DepthStencilWrite);
			depth = view.GetDepthStencil(readOnly);

			if (depthDesc->DepthLoad == Rhi::LoadOp::Clear)
			{
				const auto format = view.GetD3D12Texture().GetDesc().PixelFormat;
				const auto flags = D3D12_CLEAR_FLAG_DEPTH | (Rhi::HasStencil(format) ? D3D12_CLEAR_FLAG_STENCIL : D3D12_CLEAR_FLAGS(0));
				list->ClearDepthStencilView(depth, flags, depthDesc->ClearDepth, static_cast<UINT8>(depthDesc->ClearStencil), 1, &area);
			}
		}

		list->OMSetRenderTargets(static_cast<UINT>(count), count ? targets.data() : nullptr, FALSE, depth.ptr ? &depth : nullptr);
		rendering = true;
	}

	void D3D12CommandList::EndRendering()
	{
		if (!rendering)
		{
			throw std::logic_error("D3D12: EndRendering outside rendering");
		}

		rendering = false;
	}

	void D3D12CommandList::SetViewport(const Rhi::Viewport& viewport)
	{
		const D3D12_VIEWPORT native{ viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinDepth, viewport.MaxDepth };
		list->RSSetViewports(1, &native);
	}

	void D3D12CommandList::SetScissor(const Rhi::ScissorRect& scissor)
	{
		const D3D12_RECT rect{ scissor.X, scissor.Y, static_cast<LONG>(scissor.X + static_cast<std::int64_t>(scissor.Width)),
			static_cast<LONG>(scissor.Y + static_cast<std::int64_t>(scissor.Height)) };
		list->RSSetScissorRects(1, &rect);
	}

	void D3D12CommandList::BindVertexBuffer(std::uint32_t slot, Rhi::Buffer& buffer, std::uint64_t offset)
	{
		auto& native = static_cast<D3D12Buffer&>(buffer);
		const auto stride = graphicsLayout ? VertexStride(slot) : 0u;
		const D3D12_VERTEX_BUFFER_VIEW view{ native.GetAddress() + offset, static_cast<UINT>(native.GetDesc().Size - offset), stride };
		vertexBuffers[slot] = view;
		list->IASetVertexBuffers(slot, 1, &view);
	}

	void D3D12CommandList::BindIndexBuffer(Rhi::Buffer& buffer, std::uint64_t offset, Rhi::IndexType indexType)
	{
		auto& native = static_cast<D3D12Buffer&>(buffer);
		const D3D12_INDEX_BUFFER_VIEW view{ native.GetAddress() + offset, static_cast<UINT>(native.GetDesc().Size - offset),
			indexType == Rhi::IndexType::Uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		list->IASetIndexBuffer(&view);
	}

	void D3D12CommandList::Draw(
		std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
	{
		PrepareDraw(firstVertex, firstInstance);
		list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void D3D12CommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
		std::int32_t vertexOffset, std::uint32_t firstInstance)
	{
		PrepareDraw(static_cast<std::uint32_t>(vertexOffset), firstInstance);
		list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void D3D12CommandList::ExecuteIndirect(int kind, Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t maxCount,
		Rhi::Buffer* count, std::uint64_t countOffset, std::uint32_t stride)
	{
		FlushCopies();

		if (kind == 2)
		{
			ApplyRootBindings(true);
		}
		else
		{
			PrepareDraw(0, 0);
		}

		auto* signature = GetIndirectSignature(kind, stride);
		auto* countResource = count ? static_cast<D3D12Buffer*>(count)->GetResource() : nullptr;
		list->ExecuteIndirect(signature, maxCount, static_cast<D3D12Buffer&>(arguments).GetResource(), offset, countResource, countOffset);
	}

	void D3D12CommandList::DrawIndexedIndirect(Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
	{
		ExecuteIndirect(1, arguments, offset, drawCount, nullptr, 0, stride);
	}

	void D3D12CommandList::DrawIndexedIndirectCount(Rhi::Buffer& arguments, std::uint64_t offset, Rhi::Buffer& count,
		std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride)
	{
		ExecuteIndirect(1, arguments, offset, maxDrawCount, &count, countOffset, stride);
	}

	void D3D12CommandList::DrawIndirect(Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
	{
		ExecuteIndirect(0, arguments, offset, drawCount, nullptr, 0, stride);
	}

	void D3D12CommandList::DrawIndirectCount(Rhi::Buffer& arguments, std::uint64_t offset, Rhi::Buffer& count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride)
	{
		ExecuteIndirect(0, arguments, offset, maxDrawCount, &count, countOffset, stride);
	}

	void D3D12CommandList::Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
	{
		if (rendering)
		{
			throw std::logic_error("D3D12: Dispatch inside rendering");
		}

		// An empty grid is a valid no-op, as on Vulkan; the debug layer warns about it.
		if (x == 0 || y == 0 || z == 0)
		{
			return;
		}

		FlushCopies();
		ApplyRootBindings(true);
		list->Dispatch(x, y, z);
	}

} // namespace Swim::RhiD3D12
