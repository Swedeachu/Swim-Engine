#include "Engine/Memory/FrameArena.h"
#include "Engine/Memory/ScratchArena.h"
#include "Tests/Framework/Test.h"

#include <cstdint>
#include <stdexcept>

SWIM_TEST("Memory.LinearArena", "AlignedChunkedAllocation")
{
	Swim::Memory::LinearArena arena(128);
	void* first = arena.Allocate(17, 16);
	void* second = arena.Allocate(257, 64);

	SWIM_REQUIRE(first != nullptr);
	SWIM_REQUIRE(second != nullptr);
	SWIM_CHECK(reinterpret_cast<std::uintptr_t>(first) % 16 == 0);
	SWIM_CHECK(reinterpret_cast<std::uintptr_t>(second) % 64 == 0);
	SWIM_CHECK(arena.GetStats().BlockCount >= 2);
}

SWIM_TEST("Memory.LinearArena", "MarkerRewind")
{
	Swim::Memory::LinearArena arena(128);
	arena.Allocate(17, 16);

	const Swim::Memory::ArenaMarker marker = arena.GetMarker();
	auto* temporary = arena.Construct<std::uint64_t>(0x12345678ull);
	SWIM_REQUIRE(temporary != nullptr);
	SWIM_CHECK_EQUAL(*temporary, 0x12345678ull);

	const std::size_t usedAfterTemporary = arena.GetStats().UsedBytes;
	arena.Rewind(marker);
	SWIM_CHECK(arena.GetStats().UsedBytes < usedAfterTemporary);
}

SWIM_TEST("Memory.LinearArena", "FabricatedForwardMarkerIsRejected")
{
	Swim::Memory::LinearArena arena(128);
	arena.Allocate(32);

	SWIM_CHECK_THROWS(
		[&]
		{
			auto invalidMarker = arena.GetMarker();
			invalidMarker.Offset += 1;
			arena.Rewind(invalidMarker);
		}(),
		std::out_of_range);
}

SWIM_TEST("Memory.LinearArena", "ResetRetainsBlocksAndInvalidatesMarkers")
{
	Swim::Memory::LinearArena arena(128);
	arena.Allocate(96);

	const auto staleMarker = arena.GetMarker();
	arena.Reset();
	SWIM_CHECK_EQUAL(arena.GetStats().UsedBytes, std::size_t{ 0 });
	SWIM_CHECK(arena.GetStats().ReservedBytes > 0);
	SWIM_CHECK_THROWS(arena.Rewind(staleMarker), std::logic_error);
}

SWIM_TEST("Memory.FrameArena", "FrameResetKeepsCapacity")
{
	Swim::Memory::FrameArena frameArena(128);
	frameArena.BeginFrame(41);
	frameArena.Allocate(96);
	SWIM_CHECK(frameArena.GetStats().UsedBytes > 0);

	frameArena.BeginFrame(42);
	SWIM_CHECK_EQUAL(frameArena.GetFrameIndex(), std::uint64_t{ 42 });
	SWIM_CHECK_EQUAL(frameArena.GetStats().UsedBytes, std::size_t{ 0 });
	SWIM_CHECK(frameArena.GetStats().ReservedBytes > 0);
}

SWIM_TEST("Memory.ScratchArena", "ScopeRewindsThreadScratch")
{
	auto& scratch = Swim::Memory::GetThreadScratchArena();
	scratch.Reset();
	scratch.Allocate(32);

	const std::size_t outerUsed = scratch.GetStats().UsedBytes;
	{
		Swim::Memory::ScratchScope scope;
		scope.GetArena().Allocate(128);
		SWIM_CHECK(scratch.GetStats().UsedBytes > outerUsed);
	}
	SWIM_CHECK_EQUAL(scratch.GetStats().UsedBytes, outerUsed);
}
