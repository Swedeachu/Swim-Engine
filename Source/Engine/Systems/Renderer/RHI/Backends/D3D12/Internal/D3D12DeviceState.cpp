#include "Engine/Systems/Renderer/RHI/Backends/D3D12/Internal/D3D12DeviceState.h"

#include "Engine/Logging/Log.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace Swim::RhiD3D12
{

	D3D12DescriptorHeap::D3D12DescriptorHeap(
		ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacityValue, bool shaderVisible, std::string_view name)
		: capacity(capacityValue)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)), "CreateDescriptorHeap");
		SetDebugName(heap.Get(), name);
		increment = device->GetDescriptorHandleIncrementSize(type);
		cpuStart = heap->GetCPUDescriptorHandleForHeapStart();

		if (shaderVisible)
		{
			gpuStart = heap->GetGPUDescriptorHandleForHeapStart();
		}

		freeRanges.emplace(0u, capacity);
	}

	std::uint32_t D3D12DescriptorHeap::Allocate(std::uint32_t count)
	{
		std::scoped_lock lock(mutex);

		for (auto range = freeRanges.begin(); range != freeRanges.end(); ++range)
		{
			if (range->second >= count)
			{
				const auto start = range->first;
				const auto remaining = range->second - count;
				freeRanges.erase(range);

				if (remaining != 0)
				{
					freeRanges.emplace(start + count, remaining);
				}

				return start;
			}
		}

		throw std::runtime_error("D3D12 descriptor heap is full");
	}

	void D3D12DescriptorHeap::Free(std::uint32_t index, std::uint32_t count)
	{
		if (index == Invalid || count == 0)
		{
			return;
		}

		std::scoped_lock lock(mutex);
		auto [range, inserted] = freeRanges.emplace(index, count);

		if (const auto next = std::next(range); next != freeRanges.end() && range->first + range->second == next->first)
		{
			range->second += next->second;
			freeRanges.erase(next);
		}

		if (range != freeRanges.begin())
		{
			const auto previous = std::prev(range);

			if (previous->first + previous->second == range->first)
			{
				previous->second += range->second;
				freeRanges.erase(range);
			}
		}
	}

	void D3D12DeviceState::RecordLoss(std::string_view operation, HRESULT result) const
	{
		if (!Diagnostics->TryRecordLoss(operation, static_cast<std::int32_t>(result)))
		{
			return;
		}

		Rhi::DeviceFaultDetails details;
		details.NativeResult = Device ? static_cast<std::int32_t>(Device->GetDeviceRemovedReason()) : 0;
		details.Description = "device removed";
		details.Status = Rhi::DeviceFaultStatus::Complete;
		ComPtr<ID3D12DeviceRemovedExtendedData> dred;

		if (Device && SUCCEEDED(Device.As(&dred)))
		{
			const auto unfinished = [&](const auto* node, std::string label)
			{
				const auto done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0u;

				if (done >= node->BreadcrumbCount || details.Entries.size() >= 32)
				{
					return;
				}

				const auto next = node->pCommandHistory ? static_cast<int>(node->pCommandHistory[done]) : -1;
				details.Entries.push_back(std::string("unfinished command list ") +
					(node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "?") + " on queue " +
					(node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "?") + ": " + std::to_string(done) + "/" +
					std::to_string(node->BreadcrumbCount) + " operations done, next operation " + std::to_string(next) + label);
			};
			ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs1{};
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};

			if (SUCCEEDED(dred.As(&dred1)) && SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&breadcrumbs1)))
			{
				for (auto* node = breadcrumbs1.pHeadAutoBreadcrumbNode; node; node = node->pNext)
				{
					// The latest debug label recorded before the unfinished operation.
					const auto done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0u;
					std::string label;

					for (UINT c = 0; c < node->BreadcrumbContextsCount; ++c)
					{
						const auto& context = node->pBreadcrumbContexts[c];

						if (context.BreadcrumbIndex <= done && context.pContextString)
						{
							label = ", in \"" + Narrow(context.pContextString) + "\"";
						}
					}

					unfinished(node, label);
				}
			}
			else if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)))
			{
				for (auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node; node = node->pNext)
				{
					unfinished(node, {});
				}
			}

			D3D12_DRED_PAGE_FAULT_OUTPUT fault{};

			if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault)) && fault.PageFaultVA != 0)
			{
				char address[32];
				std::snprintf(address, sizeof(address), "0x%llx", static_cast<unsigned long long>(fault.PageFaultVA));
				details.Entries.push_back(std::string("page fault at ") + address);
				const auto list = [&](const D3D12_DRED_ALLOCATION_NODE* node, const char* kind)
				{
					for (std::size_t count = 0; node && count < 16; node = node->pNext, ++count)
					{
						// SetName stores wide names: the narrow one is usually empty.
						const std::string name = node->ObjectNameA ? std::string(node->ObjectNameA)
							: node->ObjectNameW					   ? Narrow(node->ObjectNameW)
																   : std::string("?");
						details.Entries.push_back(
							std::string(kind) + " allocation there: " + name + " (type " + std::to_string(int(node->AllocationType)) + ")");
					}
				};
				list(fault.pHeadExistingAllocationNode, "live");
				list(fault.pHeadRecentFreedAllocationNode, "recently freed");
			}
		}

		Diagnostics->CompleteFaultCapture(std::move(details));
	}

	void RecordDebugMessage(Rhi::DiagnosticLog& log, bool echo, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
		std::string_view description) noexcept
	{
		const char* label = severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ? "CORRUPTION"
			: severity == D3D12_MESSAGE_SEVERITY_ERROR					  ? "ERROR"
			: severity == D3D12_MESSAGE_SEVERITY_WARNING				  ? "WARNING"
																		  : nullptr;

		if (!label)
		{
			return;
		}

		const auto level = severity == D3D12_MESSAGE_SEVERITY_WARNING ? Rhi::DiagnosticSeverity::Warning : Rhi::DiagnosticSeverity::Error;

		try
		{
			const std::string name = "D3D12-" + std::to_string(static_cast<int>(id));
			log.Record(level, name, description);

			if (echo)
			{
				// Debug-layer errors and corruption are errors in the engine log, warnings warnings.
				Engine::Logging::Write(level == Rhi::DiagnosticSeverity::Warning ? Engine::Logging::Level::Warning : Engine::Logging::Level::Error,
					std::string("[RHI diagnostic] D3D12 ") + label + " #" + std::to_string(static_cast<int>(id)) + ": " +
						std::string(description));
			}
		}
		catch (...)
		{
		}
	}

	void D3D12DeviceState::ReportOnce(std::string key, std::string_view id, std::string_view text)
	{
		{
			std::scoped_lock lock(ReportedMutex);

			if (!Reported.insert(std::move(key)).second)
			{
				return;
			}
		}

		try
		{
			Log->Record(Rhi::DiagnosticSeverity::Error, id, text);

			if (EchoMessages)
			{
				Engine::Logging::Write(Engine::Logging::Level::Error,
					"[RHI diagnostic] D3D12 ERROR (RHI check) " + std::string(id) + ": " + std::string(text));
			}
		}
		catch (...)
		{
		}
	}

	std::size_t D3D12GpuProgress::Register(ComPtr<ID3D12Fence> fence, bool direct)
	{
		std::scoped_lock lock(mutex);
		fences.push_back(std::move(fence));
		submitted.push_back(0);
		directs.push_back(direct);
		return fences.size() - 1;
	}

	void D3D12GpuProgress::Submitted(std::size_t queue, std::uint64_t value)
	{
		std::scoped_lock lock(mutex);
		submitted[queue] = value;
	}

	std::vector<std::uint64_t> D3D12GpuProgress::Needed()
	{
		std::scoped_lock lock(mutex);
		auto needed = submitted;

		for (std::size_t queue = 0; queue < needed.size(); ++queue)
		{
			if (directs[queue] && needed[queue] != 0)
			{
				needed[queue] += 1;
			}
		}

		return needed;
	}

	bool D3D12GpuProgress::Passed(const std::vector<std::uint64_t>& needed)
	{
		std::scoped_lock lock(mutex);

		for (std::size_t queue = 0; queue < needed.size() && queue < fences.size(); ++queue)
		{
			// A lost device reports UINT64_MAX: everything has passed.
			if (needed[queue] != 0 && fences[queue]->GetCompletedValue() < needed[queue])
			{
				return false;
			}
		}

		return true;
	}

	void D3D12DeferredReleases::Release(ComPtr<ID3D12Resource> resource, D3D12Placement placement, D3D12MemoryRecord memory) noexcept
	{
		try
		{
			auto needed = progress.Needed();

			if (progress.Passed(needed))
			{
				return; // Nothing in flight: released on return.
			}

			std::scoped_lock lock(mutex);
			entries.push_back({ std::move(resource), std::move(placement), std::move(memory), std::move(needed) });
		}
		catch (...)
		{
			// Out of memory for the record: released at once, as before.
		}
	}

	void D3D12DeferredReleases::Collect() noexcept
	{
		std::vector<Entry> done;

		{
			std::scoped_lock lock(mutex);

			for (auto entry = entries.begin(); entry != entries.end();)
			{
				if (progress.Passed(entry->Needed))
				{
					done.push_back(std::move(*entry));
					entry = entries.erase(entry);
				}
				else
				{
					++entry;
				}
			}
		}
	}

	std::size_t D3D12DeferredReleases::GetPending()
	{
		std::scoped_lock lock(mutex);
		return entries.size();
	}

	D3D12Placement::~D3D12Placement()
	{
		if (owner)
		{
			owner->Free(*this);
		}
	}

	D3D12Placement D3D12PlacedHeaps::Allocate(ID3D12Device* device, bool tier2, bool buffer,
		const D3D12_RESOURCE_ALLOCATION_INFO& info, ID3D12Heap*& heap, std::uint64_t& offset)
	{
		D3D12Placement placement;

		if (info.SizeInBytes == 0 || info.SizeInBytes > BlockSize / 2 || info.Alignment > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
		{
			return placement;
		}

		const std::uint32_t kind = tier2 || buffer ? 0u : 1u;
		const auto size = AlignUp64(info.SizeInBytes, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		std::scoped_lock lock(mutex);
		auto& list = blocks[kind];
		Sweep(list);

		const auto take = [&](Block& block) -> bool
		{
			for (auto range = block.FreeRanges.begin(); range != block.FreeRanges.end(); ++range)
			{
				if (range->second < size)
				{
					continue;
				}

				const auto start = range->first;
				const auto remaining = range->second - size;
				block.FreeRanges.erase(range);

				if (remaining)
				{
					block.FreeRanges.emplace(start + size, remaining);
				}

				block.Used += size;
				placement.owner = this;
				placement.block = &block;
				placement.offset = start;
				placement.size = size;
				heap = block.Heap.Get();
				offset = start;
				return true;
			}

			return false;
		};

		for (auto& block : list)
		{
			if (take(*block))
			{
				return placement;
			}
		}

		D3D12_HEAP_DESC desc{};
		desc.SizeInBytes = BlockSize;
		desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		desc.Flags = tier2 ? D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES
			: buffer	   ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
						   : D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
		auto block = std::make_unique<Block>();
		block->Kind = kind;

		if (FAILED(device->CreateHeap(&desc, IID_PPV_ARGS(&block->Heap))))
		{
			return placement; // Out of memory for a whole block: the caller commits the resource instead.
		}

		block->Heap->SetName(L"Swim placed resources");
		block->FreeRanges.emplace(0, BlockSize);
		list.push_back(std::move(block));
		take(*list.back());
		return placement;
	}

	std::uint64_t D3D12PlacedHeaps::GetBlockCount()
	{
		std::scoped_lock lock(mutex);
		return blocks[0].size() + blocks[1].size();
	}

	void D3D12PlacedHeaps::Free(D3D12Placement& placement) noexcept
	{
		std::scoped_lock lock(mutex);
		auto& block = *static_cast<Block*>(placement.block);
		auto next = block.FreeRanges.emplace(placement.offset, placement.size).first;

		if (const auto after = std::next(next); after != block.FreeRanges.end() && next->first + next->second == after->first)
		{
			next->second += after->second;
			block.FreeRanges.erase(after);
		}

		if (next != block.FreeRanges.begin())
		{
			if (const auto before = std::prev(next); before->first + before->second == next->first)
			{
				before->second += next->second;
				block.FreeRanges.erase(next);
			}
		}

		block.Used -= placement.size;
		placement.owner = nullptr;
		auto& list = blocks[block.Kind];

		// An empty block is released while another of its kind remains (the last one stays for
		// reuse), but only once the GPU is past the work submitted so far (Sweep).
		if (block.Used == 0)
		{
			try
			{
				block.EmptyAt = progress.Needed();
			}
			catch (...)
			{
				block.EmptyAt.assign(1, UINT64_MAX); // Never passes: the block stays.
			}
		}

		Sweep(list);
	}

	void D3D12PlacedHeaps::Sweep(std::vector<std::unique_ptr<Block>>& list) noexcept
	{
		for (auto entry = list.begin(); entry != list.end() && list.size() > 1;)
		{
			if ((*entry)->Used == 0 && progress.Passed((*entry)->EmptyAt))
			{
				entry = list.erase(entry);
			}
			else
			{
				++entry;
			}
		}
	}

	void D3D12DeviceState::DrainMessages()
	{
		if (!PolledMessages)
		{
			return;
		}

		std::scoped_lock lock(PolledMessagesMutex);
		const auto count = PolledMessages->GetNumStoredMessagesAllowedByRetrievalFilter();
		std::vector<std::byte> storage;

		for (UINT64 index = 0; index < count; ++index)
		{
			SIZE_T bytes = 0;

			if (FAILED(PolledMessages->GetMessage(index, nullptr, &bytes)) || bytes == 0)
			{
				continue;
			}

			storage.resize(bytes);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());

			if (SUCCEEDED(PolledMessages->GetMessage(index, message, &bytes)))
			{
				RecordDebugMessage(*Log, EchoMessages, message->Severity, message->ID,
					std::string_view(message->pDescription, message->DescriptionByteLength ? message->DescriptionByteLength - 1 : 0));
			}
		}

		PolledMessages->ClearStoredMessages();
	}

	ID3D12CommandSignature* D3D12DeviceState::GetCommandSignature(int kind, std::uint32_t stride)
	{
		std::scoped_lock lock(SignatureMutex);
		auto& signature = CommandSignatures[{ kind, stride }];

		if (!signature)
		{
			D3D12_INDIRECT_ARGUMENT_DESC argument{};
			argument.Type = kind == 0 ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
				: kind == 1			  ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
									  : D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
			D3D12_COMMAND_SIGNATURE_DESC desc{};
			desc.ByteStride = stride;
			desc.NumArgumentDescs = 1;
			desc.pArgumentDescs = &argument;
			Check(Device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&signature)), "CreateCommandSignature", this);
		}

		return signature.Get();
	}

} // namespace Swim::RhiD3D12
