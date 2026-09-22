#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"
#include "Tests/Framework/Test.h"

using namespace Swim::Render;

SWIM_TEST("Render.GeometryRangeAllocator", "AllocatesAlignedBestFitAndCoalesces")
{
	SWIM_CHECK_THROWS(GeometryRangeAllocator(0), std::invalid_argument);
	GeometryRangeAllocator allocator(1024);
	SWIM_CHECK_THROWS(allocator.Allocate(0, 4), std::invalid_argument);
	SWIM_CHECK_THROWS(allocator.Allocate(4, 0), std::invalid_argument);

	auto a = allocator.Allocate(100, 4);
	auto b = allocator.Allocate(36, 12); // Non-power-of-two vertex stride alignment.
	auto c = allocator.Allocate(200, 16);
	SWIM_REQUIRE(a && b && c);
	SWIM_CHECK_EQUAL(a->Offset, 0u);
	SWIM_CHECK_EQUAL(b->Offset % 12, 0u);
	SWIM_CHECK(b->Offset >= 100u);
	SWIM_CHECK_EQUAL(c->Offset % 16, 0u);
	SWIM_CHECK_EQUAL(allocator.GetAllocatedBytes(), 336u);
	SWIM_CHECK_EQUAL(allocator.GetAllocationCount(), 3u);

	// Freeing the middle leaves a hole; best fit places a small request there.
	allocator.Free(*b);
	auto small = allocator.Allocate(8, 4);
	SWIM_REQUIRE(small);
	SWIM_CHECK(small->Offset >= 100u && small->Offset + 8 <= c->Offset);
	allocator.Free(*small);

	allocator.Free(*a);
	allocator.Free(*c);
	SWIM_CHECK_EQUAL(allocator.GetFreeRangeCount(), 1u);
	SWIM_CHECK_EQUAL(allocator.GetLargestFreeRange(), 1024u);
	SWIM_CHECK_EQUAL(allocator.GetAllocatedBytes(), 0u);
	SWIM_CHECK_THROWS(allocator.Free(*a), std::logic_error);
	SWIM_CHECK_THROWS(allocator.Free({ 3, 5 }), std::logic_error);
}

SWIM_TEST("Render.GeometryRangeAllocator", "ExhaustionLeavesStateUnchangedAndReportsFragmentation")
{
	GeometryRangeAllocator allocator(256);
	std::vector<GeometryRange> ranges;
	for (int i = 0; i < 8; ++i)
	{
		auto range = allocator.Allocate(32, 32);
		SWIM_REQUIRE(range);
		ranges.push_back(*range);
	}
	SWIM_CHECK(!allocator.Allocate(1, 1));
	SWIM_CHECK_EQUAL(allocator.GetFreeBytes(), 0u);
	SWIM_CHECK_EQUAL(allocator.GetFreeRangeCount(), 0u);

	// Free every other block: 128 bytes free, but no range larger than 32.
	for (std::size_t i = 0; i < ranges.size(); i += 2)
	{
		allocator.Free(ranges[i]);
	}
	SWIM_CHECK_EQUAL(allocator.GetFreeBytes(), 128u);
	SWIM_CHECK_EQUAL(allocator.GetLargestFreeRange(), 32u);
	SWIM_CHECK_EQUAL(allocator.GetFreeRangeCount(), 4u);
	SWIM_CHECK(!allocator.Allocate(64, 1));
	SWIM_CHECK_EQUAL(allocator.GetFreeRangeCount(), 4u);
	// Blocks at 64 and 128 cannot satisfy 48-byte alignment without padding past their end.
	auto aligned = allocator.Allocate(32, 48);
	SWIM_REQUIRE(aligned);
	SWIM_CHECK(aligned->Offset == 0u || aligned->Offset == 192u);
	allocator.Free(*aligned);
	allocator.Free(ranges[1]);
	SWIM_CHECK_EQUAL(allocator.GetLargestFreeRange(), 96u);
	auto merged = allocator.Allocate(96, 32);
	SWIM_REQUIRE(merged);
	SWIM_CHECK_EQUAL(merged->Offset, 0u);
}
