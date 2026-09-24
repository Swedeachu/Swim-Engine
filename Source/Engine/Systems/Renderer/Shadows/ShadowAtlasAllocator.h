#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace Swim::Render
{
	// A square shadow-map tile in atlas pixels.
	struct ShadowTile
	{
		std::uint32_t X = 0;
		std::uint32_t Y = 0;
		std::uint32_t Size = 0;

		bool operator==(const ShadowTile&) const = default;
	};

	// One light's tiles: Count tiles of one size (a spot 1, a point 6, a cascade set
	// Count). Key identifies the light across frames; larger Priority places first.
	struct ShadowTileRequest
	{
		std::uint64_t Key = 0;
		std::uint32_t Size = 0; // Power of two, MinTile .. atlas size.
		std::uint32_t Count = 1;
		float Priority = 0.0f;
	};

	struct ShadowTileAllocation
	{
		std::vector<ShadowTile> Tiles; // Empty: evicted (no room even at MinTile).
		bool Reused = false;		   // Every tile is last frame's tile.
		bool Downgraded = false;	   // Smaller than requested.
	};

	struct ShadowAtlasStats
	{
		std::uint32_t Requests = 0;
		std::uint32_t Placed = 0;
		std::uint32_t Reused = 0;
		std::uint32_t Downgraded = 0;
		std::uint32_t Evicted = 0;
		std::uint64_t UsedTexels = 0;
	};

	// Power-of-two tiles in a square atlas (Phase 16 item 71). Priority order is
	// Priority descending, then Key (ties are deterministic). Every frame:
	//  1. every request whose requested size and count are unchanged keeps last
	//     frame's tiles, downgraded or not (stable allocation: no shimmer from moving
	//     tiles, no churn from new lights);
	//  2. the others are placed in priority order, first-fit in Morton order at the
	//     requested size, halving down to MinTile when the atlas is full (downgrade);
	//  3. a request that does not fit even at MinTile displaces kept tiles of
	//     lower-priority requests, lowest first (they are placed again in their turn);
	//     when nothing lower is left to displace it is evicted;
	//  4. kept downgraded tiles move to a larger size when one is free again.
	// All tiles of a request share one size and are placed or evicted together.
	class ShadowAtlasAllocator
	{
	  public:
		// Throws std::invalid_argument unless both sizes are powers of two and minTile <= atlasSize.
		ShadowAtlasAllocator(std::uint32_t atlasSize, std::uint32_t minTile);

		// One allocation per request, in request order. Replaces last frame's placement.
		// Throws std::invalid_argument for a size that is not a power of two in range,
		// a zero count or duplicate keys.
		std::vector<ShadowTileAllocation> Allocate(std::span<const ShadowTileRequest> requests);

		std::uint32_t GetAtlasSize() const { return atlasSize; }

		std::uint32_t GetMinTile() const { return minTile; }

		const ShadowAtlasStats& GetStats() const { return stats; }

		// Forget last frame's placement (for example after a resize).
		void Reset() { previous.clear(); }

	  private:
		bool IsFree(const ShadowTile& tile) const;
		void Mark(const ShadowTile& tile);
		void Unmark(const ShadowTile& tile);
		bool TryPlace(std::uint32_t size, std::uint32_t count, std::vector<ShadowTile>& tiles);

		std::uint32_t atlasSize;
		std::uint32_t minTile;
		std::uint32_t cells; // Atlas edge in MinTile cells.
		std::vector<bool> occupied;

		struct PreviousTiles
		{
			std::uint32_t RequestedSize = 0;
			std::vector<ShadowTile> Tiles;
		};

		std::map<std::uint64_t, PreviousTiles> previous;
		ShadowAtlasStats stats;
	};
} // namespace Swim::Render
