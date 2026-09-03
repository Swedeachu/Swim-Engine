#include "LinearArena.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>

#if SWIM_MEMORY_USE_MIMALLOC
#include <mimalloc.h>
#endif

namespace Swim::Memory
{

	namespace
	{

		constexpr std::size_t MinimumBlockAlignment = alignof(std::max_align_t);

		bool IsPowerOfTwo(std::size_t value)
		{
			return value != 0 && std::has_single_bit(value);
		}

		std::size_t AlignUp(std::size_t value, std::size_t alignment)
		{
			if (!IsPowerOfTwo(alignment))
			{
				throw std::invalid_argument("LinearArena alignment must be a non-zero power of two");
			}
			if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1))
			{
				throw std::bad_alloc();
			}
			return (value + alignment - 1) & ~(alignment - 1);
		}

		void* AllocateBacking(std::size_t sizeBytes, std::size_t alignment)
		{
#if SWIM_MEMORY_USE_MIMALLOC
			return mi_malloc_aligned(sizeBytes, alignment);
#else
			return ::operator new(sizeBytes, std::align_val_t(alignment), std::nothrow);
#endif
		}

		void FreeBacking(void* memory, std::size_t alignment)
		{
#if SWIM_MEMORY_USE_MIMALLOC
			mi_free(memory);
#else
			::operator delete(memory, std::align_val_t(alignment));
#endif
		}

	}

	struct LinearArena::Impl
	{
		struct Block
		{
			std::byte* Data = nullptr;
			std::size_t Capacity = 0;
			std::size_t Offset = 0;
			std::size_t Alignment = MinimumBlockAlignment;
		};

		explicit Impl(std::size_t defaultBlockSizeBytes)
			: DefaultBlockSize(std::max(defaultBlockSizeBytes, MinimumBlockAlignment))
		{
		}

		~Impl()
		{
			for (Block& block : Blocks)
			{
				FreeBacking(block.Data, block.Alignment);
			}
		}

		Block& AddBlock(std::size_t minimumSize, std::size_t minimumAlignment)
		{
			const std::size_t alignment = std::max(minimumAlignment, MinimumBlockAlignment);
			const std::size_t capacity = AlignUp(std::max(DefaultBlockSize, minimumSize), alignment);
			void* memory = AllocateBacking(capacity, alignment);
			if (!memory)
			{
				throw std::bad_alloc();
			}

			ReservedBytes += capacity;
			Blocks.push_back(Block{
				static_cast<std::byte*>(memory),
				capacity,
				0,
				alignment
			});
			return Blocks.back();
		}

		std::size_t DefaultBlockSize = 0;
		std::vector<Block> Blocks;
		std::size_t ActiveBlock = 0;
		std::size_t ReservedBytes = 0;
		std::size_t UsedBytes = 0;
		std::size_t PeakUsedBytes = 0;
		std::uint64_t Generation = 1;
	};

	LinearArena::LinearArena(std::size_t defaultBlockSizeBytes)
		: impl(std::make_unique<Impl>(defaultBlockSizeBytes))
	{
	}

	LinearArena::~LinearArena() = default;
	LinearArena::LinearArena(LinearArena&&) noexcept = default;
	LinearArena& LinearArena::operator=(LinearArena&&) noexcept = default;

	void* LinearArena::Allocate(std::size_t sizeBytes, std::size_t alignment)
	{
		if (sizeBytes == 0)
		{
			return nullptr;
		}
		if (!IsPowerOfTwo(alignment))
		{
			throw std::invalid_argument("LinearArena alignment must be a non-zero power of two");
		}

		if (sizeBytes > std::numeric_limits<std::size_t>::max() - (alignment - 1))
		{
			throw std::bad_alloc();
		}
		const std::size_t minimumBlockSize = sizeBytes + alignment - 1;

		if (impl->Blocks.empty())
		{
			impl->AddBlock(minimumBlockSize, alignment);
		}

		for (std::size_t blockIndex = impl->ActiveBlock; blockIndex < impl->Blocks.size(); ++blockIndex)
		{
			auto& block = impl->Blocks[blockIndex];
			if (block.Alignment < alignment)
			{
				continue;
			}

			const std::size_t alignedOffset = AlignUp(block.Offset, alignment);
			if (alignedOffset <= block.Capacity && sizeBytes <= block.Capacity - alignedOffset)
			{
				impl->ActiveBlock = blockIndex;
				impl->UsedBytes -= block.Offset;
				block.Offset = alignedOffset + sizeBytes;
				impl->UsedBytes += block.Offset;
				impl->PeakUsedBytes = std::max(impl->PeakUsedBytes, impl->UsedBytes);
				return block.Data + alignedOffset;
			}
		}

		auto& block = impl->AddBlock(minimumBlockSize, alignment);
		impl->ActiveBlock = impl->Blocks.size() - 1;
		const std::size_t alignedOffset = AlignUp(block.Offset, alignment);
		block.Offset = alignedOffset + sizeBytes;
		impl->UsedBytes += block.Offset;
		impl->PeakUsedBytes = std::max(impl->PeakUsedBytes, impl->UsedBytes);
		return block.Data + alignedOffset;
	}

	ArenaMarker LinearArena::GetMarker() const
	{
		if (impl->Blocks.empty())
		{
			return ArenaMarker{ 0, 0, impl->Generation };
		}
		return ArenaMarker{
			impl->ActiveBlock,
			impl->Blocks[impl->ActiveBlock].Offset,
			impl->Generation
		};
	}

	void LinearArena::Rewind(ArenaMarker marker)
	{
		if (marker.Generation != impl->Generation)
		{
			throw std::logic_error("LinearArena marker belongs to an earlier reset generation");
		}
		if (impl->Blocks.empty())
		{
			if (marker.BlockIndex != 0 || marker.Offset != 0)
			{
				throw std::out_of_range("LinearArena marker is outside the arena");
			}
			return;
		}
		if (marker.BlockIndex >= impl->Blocks.size() || marker.BlockIndex > impl->ActiveBlock)
		{
			throw std::out_of_range("LinearArena marker is outside the arena");
		}
		if (marker.Offset > impl->Blocks[marker.BlockIndex].Offset)
		{
			throw std::out_of_range("LinearArena marker cannot rewind forward");
		}

		for (std::size_t blockIndex = marker.BlockIndex + 1; blockIndex < impl->Blocks.size(); ++blockIndex)
		{
			impl->Blocks[blockIndex].Offset = 0;
		}
		impl->Blocks[marker.BlockIndex].Offset = marker.Offset;
		impl->ActiveBlock = marker.BlockIndex;

		impl->UsedBytes = 0;
		for (const auto& block : impl->Blocks)
		{
			impl->UsedBytes += block.Offset;
		}
	}

	void LinearArena::Reset()
	{
		for (auto& block : impl->Blocks)
		{
			block.Offset = 0;
		}
		impl->ActiveBlock = 0;
		impl->UsedBytes = 0;
		++impl->Generation;
		if (impl->Generation == 0)
		{
			impl->Generation = 1;
		}
	}

	std::size_t LinearArena::GetDefaultBlockSize() const
	{
		return impl->DefaultBlockSize;
	}

	ArenaStats LinearArena::GetStats() const
	{
		return ArenaStats{
			impl->ReservedBytes,
			impl->UsedBytes,
			impl->PeakUsedBytes,
			impl->Blocks.size()
		};
	}

}
