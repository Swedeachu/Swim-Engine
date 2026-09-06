#include "Engine/Systems/Renderer/RHI/RhiMemoryBudget.h"
#include "Tests/Framework/Test.h"

#include <limits>

using namespace Swim;

SWIM_TEST("RHI.MemoryBudget", "UnavailableAndAvailableZeroUsageRemainDistinct")
{
	Rhi::MemoryBudgetSnapshot snapshot;
	SWIM_CHECK(!snapshot.IsAvailable());
	snapshot.Heaps.push_back({});
	SWIM_CHECK(snapshot.IsAvailable());
	SWIM_CHECK_EQUAL(snapshot.Heaps[0].UsageBytes, 0u);
	SWIM_CHECK_EQUAL(snapshot.Heaps[0].GetHeadroomBytes(), 0u);
	SWIM_CHECK(!snapshot.Heaps[0].IsOverBudget());
}

SWIM_TEST("RHI.MemoryBudget", "HeadroomSaturatesWithoutHidingOverBudgetUsage")
{
	Rhi::MemoryHeapBudget heap;
	heap.BudgetBytes = 100;
	for (auto usage : { 0ull, 50ull, 100ull, 101ull, std::numeric_limits<unsigned long long>::max() })
	{
		heap.UsageBytes = usage;
		SWIM_CHECK_EQUAL(heap.GetHeadroomBytes(), usage < 100 ? 100 - usage : 0);
		SWIM_CHECK_EQUAL(heap.IsOverBudget(), usage > 100);
		SWIM_CHECK_EQUAL(heap.UsageBytes, usage);
	}
}
