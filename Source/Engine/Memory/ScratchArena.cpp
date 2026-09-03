#include "ScratchArena.h"

namespace Swim::Memory
{

	LinearArena& GetThreadScratchArena()
	{
		thread_local LinearArena arena(256 * 1024);
		return arena;
	}

	ScratchScope::ScratchScope()
		: ScratchScope(GetThreadScratchArena())
	{
	}

	ScratchScope::ScratchScope(LinearArena& arena)
		: arena(&arena), marker(arena.GetMarker())
	{
	}

	ScratchScope::~ScratchScope()
	{
		Release();
	}

	ScratchScope::ScratchScope(ScratchScope&& other) noexcept
		: arena(other.arena), marker(other.marker)
	{
		other.arena = nullptr;
	}

	ScratchScope& ScratchScope::operator=(ScratchScope&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			arena = other.arena;
			marker = other.marker;
			other.arena = nullptr;
		}
		return *this;
	}

	void ScratchScope::Release()
	{
		if (arena)
		{
			arena->Rewind(marker);
			arena = nullptr;
		}
	}

}
