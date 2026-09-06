#pragma once

#include <cstdint>
#include <vector>

namespace Swim::Rhi
{

	enum class MemoryBudgetSource : std::uint8_t
	{
		AllocatorEstimate, // Allocator block bytes; budget is a backend heap-size heuristic.
		DriverEstimate, // Process-wide usage and budget estimates, including non-allocator memory.
	};

	struct MemoryHeapBudget
	{
		std::uint32_t HeapIndex = 0;
		std::uint64_t CapacityBytes = 0;
		bool DeviceLocal = false;
		// At least one memory type on this heap is host visible. This does not
		// imply that all allocations on the heap can be mapped (including UMA).
		bool HostVisible = false;
		// Allocations are occupied suballocations; blocks are reserved native memory.
		std::uint32_t AllocationCount = 0;
		std::uint64_t AllocationBytes = 0;
		std::uint32_t BlockCount = 0;
		std::uint64_t BlockBytes = 0;
		MemoryBudgetSource Source = MemoryBudgetSource::AllocatorEstimate;
		std::uint64_t UsageBytes = 0;
		std::uint64_t BudgetBytes = 0;

		// Headroom is an estimate, not an allocation guarantee. Preserve usage
		// above budget in the snapshot, but never underflow remaining bytes.
		std::uint64_t GetHeadroomBytes() const
		{
			return UsageBytes < BudgetBytes ? BudgetBytes - UsageBytes : 0;
		}

		bool IsOverBudget() const
		{
			return UsageBytes > BudgetBytes;
		}
	};

	// Owned point-in-time telemetry; it retains no device or allocation. Empty
	// means unavailable, not an available device with zero memory usage.
	// Counters and driver estimates can change concurrently while sampling.
	struct MemoryBudgetSnapshot
	{
		std::vector<MemoryHeapBudget> Heaps;

		bool IsAvailable() const
		{
			return !Heaps.empty();
		}
	};

} // namespace Swim::Rhi
