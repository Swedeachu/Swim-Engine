#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"

#include <stdexcept>

namespace Swim::Render
{
	GeometryRangeAllocator::GeometryRangeAllocator(std::uint64_t capacity) : capacity(capacity)
	{
		if (!capacity)
		{
			throw std::invalid_argument("GeometryRangeAllocator needs a nonzero capacity");
		}
		InsertFree(0, capacity);
	}

	std::optional<GeometryRange> GeometryRangeAllocator::Allocate(std::uint64_t size, std::uint64_t alignment)
	{
		if (!size || !alignment)
		{
			throw std::invalid_argument("GeometryRangeAllocator needs a nonzero size and alignment");
		}
		for (auto candidate = freeBySize.lower_bound(size); candidate != freeBySize.end(); ++candidate)
		{
			const auto blockOffset = candidate->second;
			const auto blockSize = candidate->first;
			const auto remainder = blockOffset % alignment;
			const auto padding = remainder ? alignment - remainder : 0;
			if (padding > blockSize || size > blockSize - padding)
			{
				continue;
			}

			const auto offset = blockOffset + padding;
			EraseFree(freeByOffset.find(blockOffset));
			if (padding)
			{
				InsertFree(blockOffset, padding);
			}
			if (const auto tail = blockSize - padding - size)
			{
				InsertFree(offset + size, tail);
			}
			allocations.emplace(offset, size);
			allocatedBytes += size;
			return GeometryRange{ offset, size };
		}
		return std::nullopt;
	}

	void GeometryRangeAllocator::Free(const GeometryRange& range)
	{
		const auto allocation = allocations.find(range.Offset);
		if (allocation == allocations.end() || allocation->second != range.Size)
		{
			throw std::logic_error("GeometryRangeAllocator freed an unknown or already freed range");
		}
		allocations.erase(allocation);
		allocatedBytes -= range.Size;

		auto offset = range.Offset;
		auto size = range.Size;
		auto next = freeByOffset.lower_bound(offset);
		if (next != freeByOffset.begin())
		{
			auto previous = std::prev(next);
			if (previous->first + previous->second == offset)
			{
				offset = previous->first;
				size += previous->second;
				EraseFree(previous);
			}
		}
		next = freeByOffset.lower_bound(offset + size);
		if (next != freeByOffset.end() && next->first == offset + size)
		{
			size += next->second;
			EraseFree(next);
		}
		InsertFree(offset, size);
	}

	std::uint64_t GeometryRangeAllocator::GetLargestFreeRange() const
	{
		return freeBySize.empty() ? 0 : std::prev(freeBySize.end())->first;
	}

	void GeometryRangeAllocator::InsertFree(std::uint64_t offset, std::uint64_t size)
	{
		freeByOffset.emplace(offset, size);
		freeBySize.emplace(size, offset);
	}

	void GeometryRangeAllocator::EraseFree(std::map<std::uint64_t, std::uint64_t>::iterator block)
	{
		auto [first, last] = freeBySize.equal_range(block->second);
		for (auto it = first; it != last; ++it)
		{
			if (it->second == block->first)
			{
				freeBySize.erase(it);
				break;
			}
		}
		freeByOffset.erase(block);
	}
} // namespace Swim::Render
