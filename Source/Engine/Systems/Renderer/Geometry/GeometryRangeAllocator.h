#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryRange.h"

#include <map>
#include <optional>

namespace Swim::Render
{
	// Variable-size offset allocator for one fixed-capacity page. Best-fit by
	// size, any nonzero alignment (vertex strides need not be powers of two), and
	// immediate coalescing on free. It tracks bytes only; it never touches GPU
	// memory, so callers free a range only after its last GPU use has completed.
	class GeometryRangeAllocator
	{
	  public:
		explicit GeometryRangeAllocator(std::uint64_t capacity);

		// Empty when no free range fits; the allocator is unchanged on failure.
		std::optional<GeometryRange> Allocate(std::uint64_t size, std::uint64_t alignment = 1);
		// Exactly a range returned by Allocate and not yet freed; anything else throws.
		void Free(const GeometryRange& range);

		std::uint64_t GetCapacity() const { return capacity; }

		std::uint64_t GetAllocatedBytes() const { return allocatedBytes; }

		std::uint64_t GetFreeBytes() const { return capacity - allocatedBytes; }

		std::uint64_t GetLargestFreeRange() const;

		std::size_t GetFreeRangeCount() const { return freeByOffset.size(); }

		std::size_t GetAllocationCount() const { return allocations.size(); }

	  private:
		void InsertFree(std::uint64_t offset, std::uint64_t size);
		void EraseFree(std::map<std::uint64_t, std::uint64_t>::iterator block);

		std::uint64_t capacity;
		std::uint64_t allocatedBytes = 0;
		std::map<std::uint64_t, std::uint64_t> freeByOffset;
		std::multimap<std::uint64_t, std::uint64_t> freeBySize;
		std::map<std::uint64_t, std::uint64_t> allocations;
	};
} // namespace Swim::Render
