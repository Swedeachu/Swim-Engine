#include "Engine/Systems/Renderer/Resources/StreamingBuffer.h"

#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Swim::Render
{

	namespace
	{

		using S = Rhi::ResourceState;

		std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

	} // namespace

	StreamingBuffer::StreamingBuffer(Rhi::Device& device, StreamingBufferDesc descValue) : desc(std::move(descValue))
	{
		if (desc.Alignment < 4 || (desc.Alignment & (desc.Alignment - 1)) != 0 || desc.Capacity < desc.Alignment)
		{
			throw std::invalid_argument("StreamingBuffer needs a power-of-two alignment of at least 4 and a capacity of at least one unit");
		}

		desc.Capacity = desc.Capacity / desc.Alignment * desc.Alignment;
		Rhi::BufferDesc bufferDesc;
		bufferDesc.Size = desc.Capacity;
		bufferDesc.Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination | desc.Usage;
		bufferDesc.DebugName = desc.DebugName;
		buffer = device.CreateBuffer(bufferDesc);

		if (!buffer)
		{
			throw std::runtime_error(desc.DebugName + ": the buffer could not be created");
		}

		ReleaseFree(0, desc.Capacity);
		stats.Capacity = desc.Capacity;
		stats.LargestFreeRange = desc.Capacity;
		stats.FreeRanges = 1;
	}

	const StreamingBuffer::Slot& StreamingBuffer::Get(StreamingRange range) const
	{
		if (!IsValid(range))
		{
			throw std::invalid_argument(desc.DebugName + ": stale or unknown range");
		}

		return slots[range.Index];
	}

	bool StreamingBuffer::IsValid(StreamingRange range) const
	{
		return range.Index < slots.size() && slots[range.Index].Live && slots[range.Index].Generation == range.Generation;
	}

	std::uint64_t StreamingBuffer::GetOffset(StreamingRange range) const
	{
		return Get(range).Offset;
	}

	std::uint64_t StreamingBuffer::GetSize(StreamingRange range) const
	{
		return Get(range).Size;
	}

	std::optional<std::uint64_t> StreamingBuffer::TakeFree(std::uint64_t size)
	{
		const auto best = freeBySize.lower_bound(size);

		if (best == freeBySize.end())
		{
			return std::nullopt;
		}

		const auto [blockSize, offset] = *best;
		freeBySize.erase(best);
		freeByOffset.erase(offset);

		if (blockSize > size)
		{
			freeByOffset.emplace(offset + size, blockSize - size);
			freeBySize.emplace(blockSize - size, offset + size);
		}

		return offset;
	}

	void StreamingBuffer::ReleaseFree(std::uint64_t offset, std::uint64_t size)
	{
		const auto eraseSized = [this](std::uint64_t blockOffset, std::uint64_t blockSize)
		{
			auto [first, last] = freeBySize.equal_range(blockSize);

			for (; first != last; ++first)
			{
				if (first->second == blockOffset)
				{
					freeBySize.erase(first);
					return;
				}
			}
		};

		auto next = freeByOffset.lower_bound(offset);

		if (next != freeByOffset.begin())
		{
			const auto previous = std::prev(next);

			if (previous->first + previous->second == offset)
			{
				eraseSized(previous->first, previous->second);
				offset = previous->first;
				size += previous->second;
				freeByOffset.erase(previous);
			}
		}

		if (next != freeByOffset.end() && offset + size == next->first)
		{
			eraseSized(next->first, next->second);
			size += next->second;
			freeByOffset.erase(next);
		}

		freeByOffset.emplace(offset, size);
		freeBySize.emplace(size, offset);
	}

	std::optional<StreamingRange> StreamingBuffer::Allocate(std::uint64_t size)
	{
		const auto aligned = AlignUp(std::max<std::uint64_t>(size, 1), desc.Alignment);
		const auto offset = TakeFree(aligned);

		if (!offset)
		{
			return std::nullopt;
		}

		std::uint32_t index;

		if (!freeSlots.empty())
		{
			index = freeSlots.back();
			freeSlots.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(slots.size());
			slots.emplace_back();
		}

		auto& slot = slots[index];
		slot.Offset = *offset;
		slot.Size = aligned;
		slot.Live = true;
		rangesByOffset.emplace(*offset, index);
		stats.AllocatedBytes += aligned;
		stats.Ranges = static_cast<std::uint32_t>(rangesByOffset.size());
		return StreamingRange{ index, slot.Generation };
	}

	void StreamingBuffer::Free(StreamingRange range)
	{
		auto& slot = const_cast<Slot&>(Get(range));
		std::erase_if(pending,
			[&](const PendingWrite& write)
			{
				return write.Range == range;
			});
		rangesByOffset.erase(slot.Offset);
		ReleaseFree(slot.Offset, slot.Size);
		stats.AllocatedBytes -= slot.Size;
		stats.Ranges = static_cast<std::uint32_t>(rangesByOffset.size());
		slot.Live = false;
		++slot.Generation;
		freeSlots.push_back(range.Index);
	}

	void StreamingBuffer::Write(StreamingRange range, std::span<const std::byte> bytes, std::uint64_t offset)
	{
		const auto& slot = Get(range);

		if (bytes.empty() || offset > slot.Size || bytes.size() > slot.Size - offset)
		{
			throw std::invalid_argument(desc.DebugName + ": a write must be nonempty and inside its range");
		}

		pending.push_back({ range, offset, std::vector<std::byte>(bytes.begin(), bytes.end()) });
	}

	bool StreamingBuffer::IsUploaded(StreamingRange range) const
	{
		return std::none_of(pending.begin(), pending.end(),
			[&](const PendingWrite& write)
			{
				return write.Range == range;
			});
	}

	void StreamingBuffer::Defragment(std::vector<Move>& moves)
	{
		std::uint64_t moved = 0;
		std::vector<std::pair<std::uint32_t, std::uint64_t>> reports;

		// From the top down (a bounded number of candidates), each range that fits in a hole
		// beneath it moves into the lowest such hole, until the free space is one block or the
		// budget is spent. A range moves once per frame: the gather copies every source before
		// any destination is written.
		constexpr std::size_t MaxCandidates = 32;
		std::vector<std::uint32_t> candidates;

		for (auto top = rangesByOffset.rbegin(); top != rangesByOffset.rend() && candidates.size() < MaxCandidates; ++top)
		{
			candidates.push_back(top->second);
		}

		for (const auto index : candidates)
		{
			if (defragmentBudget == 0 || freeByOffset.size() <= 1)
			{
				break;
			}

			auto& slot = slots[index];
			const auto offset = slot.Offset;

			if (moved + slot.Size > defragmentBudget && moved > 0)
			{
				break;
			}

			std::optional<std::uint64_t> hole;

			for (const auto& [holeOffset, holeSize] : freeByOffset)
			{
				if (holeOffset >= offset)
				{
					break;
				}

				if (holeSize >= slot.Size)
				{
					hole = holeOffset;
					break;
				}
			}

			if (!hole)
			{
				continue;
			}

			const auto holeSize = freeByOffset.at(*hole);
			auto [first, last] = freeBySize.equal_range(holeSize);

			for (; first != last; ++first)
			{
				if (first->second == *hole)
				{
					freeBySize.erase(first);
					break;
				}
			}

			freeByOffset.erase(*hole);

			if (holeSize > slot.Size)
			{
				freeByOffset.emplace(*hole + slot.Size, holeSize - slot.Size);
				freeBySize.emplace(holeSize - slot.Size, *hole + slot.Size);
			}

			rangesByOffset.erase(offset);
			ReleaseFree(offset, slot.Size);
			slot.Offset = *hole;
			rangesByOffset.emplace(*hole, index);
			moves.push_back({ offset, *hole, slot.Size });
			reports.emplace_back(index, offset);
			moved += slot.Size;
		}

		if (relocated)
		{
			for (const auto& [index, from] : reports)
			{
				relocated(StreamingRange{ index, slots[index].Generation }, from, slots[index].Offset);
			}
		}
	}

	GraphBuffer StreamingBuffer::Import(RenderGraph& graph, std::uint64_t uploadBudget)
	{
		const auto imported = graph.ImportBuffer(*buffer, S::ShaderRead);
		stats.UploadedBytes = 0;
		stats.MovedBytes = 0;
		stats.Moves = 0;

		std::vector<Move> moves;
		Defragment(moves);

		if (!moves.empty())
		{
			std::uint64_t total = 0;

			for (const auto& move : moves)
			{
				total += move.Size;
			}

			Rhi::BufferDesc scratchDesc;
			scratchDesc.Size = total;
			scratchDesc.Usage = Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination;
			const std::string scratchName = desc.DebugName + " moves";
			scratchDesc.DebugName = scratchName;
			const auto scratch = graph.CreateBuffer(scratchDesc);
			graph.AddPass(
				desc.DebugName + " gather", Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(imported, S::CopySource);
					b.Write(scratch, S::CopyDestination);
				},
				[imported, scratch, moves](RenderCommandContext& c)
				{
					const auto source = c.GetRange(imported);
					const auto destination = c.GetRange(scratch);
					std::uint64_t at = 0;

					for (const auto& move : moves)
					{
						c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer, { source.Offset + move.From, destination.Offset + at, move.Size });
						at += move.Size;
					}
				});
			graph.AddPass(
				desc.DebugName + " scatter", Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(scratch, S::CopySource);
					b.ReadWrite(imported, S::CopyDestination);
				},
				[imported, scratch, moves](RenderCommandContext& c)
				{
					const auto source = c.GetRange(scratch);
					const auto destination = c.GetRange(imported);
					std::uint64_t at = 0;

					for (const auto& move : moves)
					{
						c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer, { source.Offset + at, destination.Offset + move.To, move.Size });
						at += move.Size;
					}
				});
			stats.MovedBytes = total;
			stats.Moves = static_cast<std::uint32_t>(moves.size());
		}

		struct Chunk
		{
			std::vector<std::byte> Bytes;
			std::uint64_t StagingOffset = 0;
			std::uint64_t Destination = 0;
		};

		auto chunks = std::make_shared<std::vector<Chunk>>();
		std::uint64_t staged = 0;

		while (!pending.empty() && (chunks->empty() || staged + pending.front().Bytes.size() <= uploadBudget))
		{
			auto write = std::move(pending.front());
			pending.pop_front();
			const auto size = write.Bytes.size();
			chunks->push_back({ std::move(write.Bytes), staged, Get(write.Range).Offset + write.Offset });
			staged = AlignUp(staged + size, 16);
		}

		stats.PendingUploadBytes = 0;

		for (const auto& write : pending)
		{
			stats.PendingUploadBytes += write.Bytes.size();
		}

		if (!chunks->empty())
		{
			const std::string stagingName = desc.DebugName + " staging";
			const auto staging = graph.CreateUpload({ staged, Rhi::BufferUsage::TransferSource, 16, stagingName },
				[chunks](std::span<std::byte> bytes)
				{
					for (const auto& chunk : *chunks)
					{
						std::memcpy(bytes.data() + chunk.StagingOffset, chunk.Bytes.data(), chunk.Bytes.size());
					}
				});
			graph.AddPass(
				desc.DebugName + " upload", Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(staging, S::CopySource);
					b.ReadWrite(imported, S::CopyDestination);
				},
				[imported, staging, chunks](RenderCommandContext& c)
				{
					const auto source = c.GetRange(staging);
					const auto destination = c.GetRange(imported);

					for (const auto& chunk : *chunks)
					{
						c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer,
							{ source.Offset + chunk.StagingOffset, destination.Offset + chunk.Destination, chunk.Bytes.size() });
					}
				});

			for (const auto& chunk : *chunks)
			{
				stats.UploadedBytes += chunk.Bytes.size();
			}
		}

		stats.LargestFreeRange = freeBySize.empty() ? 0 : freeBySize.rbegin()->first;
		stats.FreeRanges = static_cast<std::uint32_t>(freeByOffset.size());
		return imported;
	}

} // namespace Swim::Render
