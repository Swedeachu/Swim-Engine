#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Swim::Memory
{

	struct ArenaMarker
	{
		std::size_t BlockIndex = 0;
		std::size_t Offset = 0;
		std::uint64_t Generation = 0;
	};

	struct ArenaStats
	{
		std::size_t ReservedBytes = 0;
		std::size_t UsedBytes = 0;
		std::size_t PeakUsedBytes = 0;
		std::size_t BlockCount = 0;
	};

	class LinearArena
	{
	public:

		explicit LinearArena(std::size_t defaultBlockSizeBytes = 64 * 1024);
		~LinearArena();

		LinearArena(const LinearArena&) = delete;
		LinearArena& operator=(const LinearArena&) = delete;
		LinearArena(LinearArena&&) noexcept;
		LinearArena& operator=(LinearArena&&) noexcept;

		void* Allocate(std::size_t sizeBytes, std::size_t alignment = alignof(std::max_align_t));

		template<typename T>
		T* AllocateArray(std::size_t count = 1)
		{
			static_assert(!std::is_void_v<T>);
			if (count == 0)
			{
				return nullptr;
			}

			if (count > static_cast<std::size_t>(-1) / sizeof(T))
			{
				throw std::bad_array_new_length();
			}

			return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
		}

		template<typename T, typename... Args>
		T* Construct(Args&&... args)
		{
			T* storage = AllocateArray<T>();
			return ::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
		}

		ArenaMarker GetMarker() const;
		void Rewind(ArenaMarker marker);
		void Reset();

		std::size_t GetDefaultBlockSize() const;
		ArenaStats GetStats() const;

	private:

		struct Impl;
		std::unique_ptr<Impl> impl;
	};

}
