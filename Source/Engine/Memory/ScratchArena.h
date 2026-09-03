#pragma once

#include "LinearArena.h"

namespace Swim::Memory
{

	LinearArena& GetThreadScratchArena();

	class ScratchScope
	{
	public:

		ScratchScope();
		explicit ScratchScope(LinearArena& arena);
		~ScratchScope();

		ScratchScope(const ScratchScope&) = delete;
		ScratchScope& operator=(const ScratchScope&) = delete;
		ScratchScope(ScratchScope&& other) noexcept;
		ScratchScope& operator=(ScratchScope&& other) noexcept;

		LinearArena& GetArena() { return *arena; }
		const LinearArena& GetArena() const { return *arena; }

	private:

		void Release();

		LinearArena* arena = nullptr;
		ArenaMarker marker{};
	};

}
