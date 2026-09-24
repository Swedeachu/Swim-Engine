#include "Engine/Systems/Renderer/Shadows/ShadowAtlasAllocator.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		bool IsPowerOfTwo(std::uint32_t value)
		{
			return value != 0 && (value & (value - 1)) == 0;
		}

		// De-interleaves a Morton index into (x, y).
		std::uint32_t Compact(std::uint32_t value)
		{
			value &= 0x55555555u;
			value = (value | (value >> 1)) & 0x33333333u;
			value = (value | (value >> 2)) & 0x0f0f0f0fu;
			value = (value | (value >> 4)) & 0x00ff00ffu;
			value = (value | (value >> 8)) & 0x0000ffffu;
			return value;
		}
	} // namespace

	ShadowAtlasAllocator::ShadowAtlasAllocator(std::uint32_t atlasSizeInput, std::uint32_t minTileInput)
		: atlasSize(atlasSizeInput), minTile(minTileInput)
	{
		if (!IsPowerOfTwo(atlasSize) || !IsPowerOfTwo(minTile) || minTile > atlasSize)
		{
			throw std::invalid_argument("Shadow atlas and minimum tile sizes must be powers of two with minTile <= atlasSize");
		}
		cells = atlasSize / minTile;
		occupied.assign(std::size_t(cells) * cells, false);
	}

	bool ShadowAtlasAllocator::IsFree(const ShadowTile& tile) const
	{
		const std::uint32_t x0 = tile.X / minTile;
		const std::uint32_t y0 = tile.Y / minTile;
		const std::uint32_t span = tile.Size / minTile;
		for (std::uint32_t y = y0; y < y0 + span; ++y)
		{
			for (std::uint32_t x = x0; x < x0 + span; ++x)
			{
				if (occupied[std::size_t(y) * cells + x])
				{
					return false;
				}
			}
		}
		return true;
	}

	void ShadowAtlasAllocator::Mark(const ShadowTile& tile)
	{
		const std::uint32_t x0 = tile.X / minTile;
		const std::uint32_t y0 = tile.Y / minTile;
		const std::uint32_t span = tile.Size / minTile;
		for (std::uint32_t y = y0; y < y0 + span; ++y)
		{
			for (std::uint32_t x = x0; x < x0 + span; ++x)
			{
				occupied[std::size_t(y) * cells + x] = true;
			}
		}
	}

	void ShadowAtlasAllocator::Unmark(const ShadowTile& tile)
	{
		const std::uint32_t x0 = tile.X / minTile;
		const std::uint32_t y0 = tile.Y / minTile;
		const std::uint32_t span = tile.Size / minTile;
		for (std::uint32_t y = y0; y < y0 + span; ++y)
		{
			for (std::uint32_t x = x0; x < x0 + span; ++x)
			{
				occupied[std::size_t(y) * cells + x] = false;
			}
		}
	}

	bool ShadowAtlasAllocator::TryPlace(std::uint32_t size, std::uint32_t count, std::vector<ShadowTile>& tiles)
	{
		// Tentatively mark tiles; roll back when the whole group does not fit.
		const auto snapshot = occupied;
		tiles.clear();
		const std::uint32_t perEdge = atlasSize / size;
		const std::uint32_t slots = perEdge * perEdge;
		for (std::uint32_t morton = 0; morton < slots && tiles.size() < count; ++morton)
		{
			const ShadowTile tile{ Compact(morton) * size, Compact(morton >> 1) * size, size };
			if (IsFree(tile))
			{
				Mark(tile);
				tiles.push_back(tile);
			}
		}
		if (tiles.size() == count)
		{
			return true;
		}
		occupied = snapshot;
		tiles.clear();
		return false;
	}

	std::vector<ShadowTileAllocation> ShadowAtlasAllocator::Allocate(std::span<const ShadowTileRequest> requests)
	{
		std::set<std::uint64_t> keys;
		for (const auto& request : requests)
		{
			if (!IsPowerOfTwo(request.Size) || request.Size < minTile || request.Size > atlasSize || request.Count == 0 ||
				!keys.insert(request.Key).second)
			{
				throw std::invalid_argument("Shadow tile requests need unique keys, a count and power-of-two sizes within the atlas");
			}
		}
		std::vector<std::size_t> order(requests.size());
		std::iota(order.begin(), order.end(), std::size_t(0));
		std::stable_sort(order.begin(), order.end(),
			[&](std::size_t a, std::size_t b)
			{
				if (requests[a].Priority != requests[b].Priority)
				{
					return requests[a].Priority > requests[b].Priority;
				}
				return requests[a].Key < requests[b].Key;
			});

		std::fill(occupied.begin(), occupied.end(), false);
		stats = {};
		stats.Requests = static_cast<std::uint32_t>(requests.size());
		std::vector<ShadowTileAllocation> result(requests.size());

		// 1. Every request whose size and count are unchanged keeps last frame's tiles
		//    (they were disjoint last frame, so they are all free now).
		for (const auto index : order)
		{
			const auto& request = requests[index];
			const auto old = previous.find(request.Key);
			if (old != previous.end() && old->second.Tiles.size() == request.Count && old->second.RequestedSize == request.Size &&
				std::all_of(old->second.Tiles.begin(), old->second.Tiles.end(),
					[&](const ShadowTile& tile)
					{
						return IsFree(tile);
					}))
			{
				for (const auto& tile : old->second.Tiles)
				{
					Mark(tile);
				}
				result[index].Tiles = old->second.Tiles;
				result[index].Reused = true;
				result[index].Downgraded = old->second.Tiles.front().Size != request.Size;
			}
		}

		// 2. The rest, by priority: first fit, halving until MinTile. A request that does
		//    not fit displaces kept tiles of lower-priority requests (lowest first); those
		//    are placed again when their turn comes.
		const auto tryPlace = [&](const ShadowTileRequest& request, ShadowTileAllocation& allocation)
		{
			for (std::uint32_t size = request.Size; size >= minTile; size /= 2)
			{
				if (TryPlace(size, request.Count, allocation.Tiles))
				{
					allocation.Downgraded = size != request.Size;
					return true;
				}
			}
			return false;
		};
		for (std::size_t position = 0; position < order.size(); ++position)
		{
			const auto index = order[position];
			auto& allocation = result[index];
			if (allocation.Reused)
			{
				continue;
			}
			bool fits = tryPlace(requests[index], allocation);
			for (std::size_t victim = order.size(); !fits && victim-- > position + 1;)
			{
				auto& displaced = result[order[victim]];
				if (!displaced.Reused)
				{
					continue;
				}
				for (const auto& tile : displaced.Tiles)
				{
					Unmark(tile);
				}
				displaced = {};
				fits = tryPlace(requests[index], allocation);
			}
		}

		// 3. Kept downgraded tiles grow back when the full size (or at least a larger
		//    one) is free now, in priority order.
		for (const auto index : order)
		{
			auto& allocation = result[index];
			if (!allocation.Reused || !allocation.Downgraded)
			{
				continue;
			}
			const auto& request = requests[index];
			const std::uint32_t current = allocation.Tiles.front().Size;
			std::vector<ShadowTile> grown;
			for (std::uint32_t size = request.Size; size > current; size /= 2)
			{
				if (TryPlace(size, request.Count, grown))
				{
					for (const auto& tile : allocation.Tiles)
					{
						Unmark(tile);
					}
					allocation.Tiles = grown;
					allocation.Reused = false;
					allocation.Downgraded = size != request.Size;
					break;
				}
			}
		}

		std::map<std::uint64_t, PreviousTiles> placed;
		for (std::size_t index = 0; index < requests.size(); ++index)
		{
			const auto& allocation = result[index];
			if (allocation.Tiles.empty())
			{
				++stats.Evicted;
				continue;
			}
			++stats.Placed;
			stats.Reused += allocation.Reused ? 1u : 0u;
			stats.Downgraded += allocation.Downgraded ? 1u : 0u;
			stats.UsedTexels += std::uint64_t(allocation.Tiles.front().Size) * allocation.Tiles.front().Size * allocation.Tiles.size();
			placed[requests[index].Key] = { requests[index].Size, allocation.Tiles };
		}
		previous = std::move(placed);
		return result;
	}
} // namespace Swim::Render
