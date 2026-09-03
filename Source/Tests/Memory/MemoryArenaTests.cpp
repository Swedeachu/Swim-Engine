#include "Engine/Memory/FrameArena.h"
#include "Engine/Memory/ScratchArena.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{

	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "Memory arena test failed: " << message << '\n';
			std::exit(1);
		}
	}

}

int main()
{
	Swim::Memory::LinearArena arena(128);
	void* first = arena.Allocate(17, 16);
	void* second = arena.Allocate(257, 64);
	Require(first != nullptr, "first allocation");
	Require(second != nullptr, "growth allocation");
	Require(reinterpret_cast<std::uintptr_t>(first) % 16 == 0, "16-byte alignment");
	Require(reinterpret_cast<std::uintptr_t>(second) % 64 == 0, "64-byte alignment");
	Require(arena.GetStats().BlockCount >= 2, "chunked growth");

	const Swim::Memory::ArenaMarker marker = arena.GetMarker();
	auto* temporary = arena.Construct<std::uint64_t>(0x12345678ull);
	Require(*temporary == 0x12345678ull, "construct");
	const std::size_t usedAfterTemporary = arena.GetStats().UsedBytes;
	arena.Rewind(marker);
	Require(arena.GetStats().UsedBytes < usedAfterTemporary, "marker rewind");

	bool rejectedForwardMarker = false;
	try
	{
		auto invalidMarker = arena.GetMarker();
		invalidMarker.Offset += 1;
		arena.Rewind(invalidMarker);
	}
	catch (const std::out_of_range&)
	{
		rejectedForwardMarker = true;
	}
	Require(rejectedForwardMarker, "fabricated forward marker rejected");

	Swim::Memory::FrameArena frameArena(128);
	frameArena.BeginFrame(41);
	frameArena.Allocate(96);
	Require(frameArena.GetStats().UsedBytes > 0, "frame allocation");
	frameArena.BeginFrame(42);
	Require(frameArena.GetFrameIndex() == 42, "frame index");
	Require(frameArena.GetStats().UsedBytes == 0, "frame reset");
	Require(frameArena.GetStats().ReservedBytes > 0, "frame arena retains capacity");

	auto& scratch = Swim::Memory::GetThreadScratchArena();
	scratch.Reset();
	scratch.Allocate(32);
	const std::size_t outerUsed = scratch.GetStats().UsedBytes;
	{
		Swim::Memory::ScratchScope scope;
		scope.GetArena().Allocate(128);
		Require(scratch.GetStats().UsedBytes > outerUsed, "scratch scope allocation");
	}
	Require(scratch.GetStats().UsedBytes == outerUsed, "scratch scope rewind");

	const auto staleMarker = arena.GetMarker();
	arena.Reset();
	Require(arena.GetStats().UsedBytes == 0, "arena reset");
	Require(arena.GetStats().ReservedBytes > 0, "arena reset retains blocks");

	bool rejectedStaleMarker = false;
	try
	{
		arena.Rewind(staleMarker);
	}
	catch (const std::logic_error&)
	{
		rejectedStaleMarker = true;
	}
	Require(rejectedStaleMarker, "stale marker rejected after reset");
	return 0;
}
