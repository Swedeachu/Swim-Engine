#pragma once

#include "LinearArena.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Swim::Memory
{

	class FrameArena
	{
	public:

		explicit FrameArena(std::size_t defaultBlockSizeBytes = 1024 * 1024)
			: arena(defaultBlockSizeBytes)
		{
		}

		void BeginFrame(std::uint64_t newFrameIndex)
		{
			arena.Reset();
			frameIndex = newFrameIndex;
		}

		void* Allocate(std::size_t sizeBytes, std::size_t alignment = alignof(std::max_align_t))
		{
			return arena.Allocate(sizeBytes, alignment);
		}

		template<typename T>
		T* AllocateArray(std::size_t count = 1)
		{
			return arena.AllocateArray<T>(count);
		}

		template<typename T, typename... Args>
		T* Construct(Args&&... args)
		{
			return arena.Construct<T>(std::forward<Args>(args)...);
		}

		std::uint64_t GetFrameIndex() const { return frameIndex; }
		ArenaStats GetStats() const { return arena.GetStats(); }
		LinearArena& GetArena() { return arena; }
		const LinearArena& GetArena() const { return arena; }

	private:

		LinearArena arena;
		std::uint64_t frameIndex = 0;
	};

}
