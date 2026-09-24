#include "Engine/Systems/Renderer/Shadows/ShadowAtlasAllocator.h"
#include "Tests/Framework/Test.h"

#include <random>
#include <stdexcept>
#include <vector>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	bool Overlap(const ShadowTile& a, const ShadowTile& b)
	{
		return a.X < b.X + b.Size && b.X < a.X + a.Size && a.Y < b.Y + b.Size && b.Y < a.Y + a.Size;
	}

	// Every tile is inside the atlas, aligned to its size and disjoint from every other.
	bool Valid(const std::vector<ShadowTileAllocation>& allocations, std::uint32_t atlasSize)
	{
		std::vector<ShadowTile> all;
		for (const auto& allocation : allocations)
		{
			for (const auto& tile : allocation.Tiles)
			{
				if (tile.Size == 0 || tile.X % tile.Size != 0 || tile.Y % tile.Size != 0 || tile.X + tile.Size > atlasSize ||
					tile.Y + tile.Size > atlasSize || tile.Size != allocation.Tiles.front().Size)
				{
					return false;
				}
				all.push_back(tile);
			}
		}
		for (std::size_t i = 0; i < all.size(); ++i)
		{
			for (std::size_t j = i + 1; j < all.size(); ++j)
			{
				if (Overlap(all[i], all[j]))
				{
					return false;
				}
			}
		}
		return true;
	}
} // namespace

SWIM_TEST("Render.Shadows.Atlas", "PacksMixedSizesWithoutOverlap")
{
	ShadowAtlasAllocator atlas(4096, 128);
	const std::vector<ShadowTileRequest> requests{
		{ 1, 1024, 3, 10.0f }, // Cascades.
		{ 2, 512, 1, 5.0f },   // Spots.
		{ 3, 512, 1, 4.0f },
		{ 4, 256, 6, 3.0f }, // Point faces.
		{ 5, 256, 6, 2.0f },
	};
	const auto allocations = atlas.Allocate(requests);
	SWIM_REQUIRE_EQUAL(allocations.size(), std::size_t(5));
	SWIM_CHECK(Valid(allocations, 4096));
	SWIM_CHECK_EQUAL(allocations[0].Tiles.size(), std::size_t(3));
	SWIM_CHECK_EQUAL(allocations[3].Tiles.size(), std::size_t(6));
	for (std::size_t i = 0; i < allocations.size(); ++i)
	{
		SWIM_CHECK(allocations[i].Tiles.front().Size == requests[i].Size);
		SWIM_CHECK(!allocations[i].Downgraded && !allocations[i].Reused);
	}
	// Highest priority is placed first: first-fit in Morton order puts it at the origin.
	SWIM_CHECK((allocations[0].Tiles[0] == ShadowTile{ 0, 0, 1024 }));
	const auto& stats = atlas.GetStats();
	SWIM_CHECK_EQUAL(stats.Requests, 5u);
	SWIM_CHECK_EQUAL(stats.Placed, 5u);
	SWIM_CHECK_EQUAL(stats.Evicted, 0u);
	SWIM_CHECK_EQUAL(stats.UsedTexels, std::uint64_t(3) * 1024 * 1024 + 2ull * 512 * 512 + 12ull * 256 * 256);
}

SWIM_TEST("Render.Shadows.Atlas", "UnchangedRequestsKeepTheirTilesAcrossFrames")
{
	ShadowAtlasAllocator atlas(2048, 128);
	std::vector<ShadowTileRequest> requests{ { 7, 512, 1, 1.0f }, { 3, 256, 6, 2.0f }, { 9, 1024, 2, 0.5f } };
	const auto first = atlas.Allocate(requests);
	SWIM_REQUIRE(Valid(first, 2048));

	// Reordered, with a new low-priority light and changed priorities: tiles stay put.
	requests = { { 9, 1024, 2, 3.0f }, { 3, 256, 6, 0.1f }, { 11, 256, 1, 0.0f }, { 7, 512, 1, 1.0f } };
	const auto second = atlas.Allocate(requests);
	SWIM_REQUIRE(Valid(second, 2048));
	SWIM_CHECK(second[0].Reused && second[0].Tiles == first[2].Tiles);
	SWIM_CHECK(second[1].Reused && second[1].Tiles == first[1].Tiles);
	SWIM_CHECK(second[3].Reused && second[3].Tiles == first[0].Tiles);
	SWIM_CHECK(!second[2].Reused && second[2].Tiles.size() == 1);
	SWIM_CHECK_EQUAL(atlas.GetStats().Reused, 3u);

	// A resized request moves; the others still reuse.
	requests[3].Size = 256;
	const auto third = atlas.Allocate(requests);
	SWIM_CHECK(Valid(third, 2048));
	SWIM_CHECK(!third[3].Reused && third[3].Tiles.front().Size == 256);
	SWIM_CHECK(third[0].Reused && third[1].Reused && third[2].Reused);

	// Reset forgets the placement.
	atlas.Reset();
	const auto fourth = atlas.Allocate(requests);
	for (const auto& allocation : fourth)
	{
		SWIM_CHECK(!allocation.Reused);
	}
}

SWIM_TEST("Render.Shadows.Atlas", "FullAtlasDowngradesThenEvictsLowestPriority")
{
	ShadowAtlasAllocator atlas(1024, 128);
	// 1024^2 = four 512 tiles. Six requests for 512, listed lowest priority first:
	// the four highest priorities fill the atlas and the two lowest are evicted.
	std::vector<ShadowTileRequest> requests;
	for (std::uint64_t key = 0; key < 6; ++key)
	{
		requests.push_back({ key, 512, 1, float(key) });
	}
	const auto allocations = atlas.Allocate(requests);
	SWIM_CHECK(Valid(allocations, 1024));
	SWIM_CHECK(allocations[0].Tiles.empty() && allocations[1].Tiles.empty());
	for (std::size_t i = 2; i < 6; ++i)
	{
		SWIM_CHECK(allocations[i].Tiles.size() == 1 && allocations[i].Tiles.front().Size == 512 && !allocations[i].Downgraded);
	}
	SWIM_CHECK_EQUAL(atlas.GetStats().Evicted, 2u);

	// Lower-priority lights that still fit at a smaller size are downgraded instead.
	ShadowAtlasAllocator mixed(1024, 128);
	const std::vector<ShadowTileRequest> overflow{
		{ 1, 512, 1, 9.0f }, { 2, 512, 1, 8.0f }, { 3, 512, 1, 7.0f }, { 4, 256, 3, 6.0f }, // Three of the last quadrant's four 256 cells.
		{ 5, 512, 1, 5.0f },																// Halves to 256.
		{ 6, 256, 1, 4.0f },																// Full: evicted.
	};
	const auto mixedAllocations = mixed.Allocate(overflow);
	SWIM_CHECK(Valid(mixedAllocations, 1024));
	SWIM_CHECK(mixedAllocations[3].Tiles.size() == 3 && !mixedAllocations[3].Downgraded);
	SWIM_CHECK(mixedAllocations[4].Tiles.front().Size == 256 && mixedAllocations[4].Downgraded);
	SWIM_CHECK(mixedAllocations[5].Tiles.empty());
	SWIM_CHECK_EQUAL(mixed.GetStats().Downgraded, 1u);
	SWIM_CHECK_EQUAL(mixed.GetStats().Evicted, 1u);
}

SWIM_TEST("Render.Shadows.Atlas", "DowngradeHalvesUntilTheRequestFits")
{
	ShadowAtlasAllocator atlas(1024, 128);
	const std::vector<ShadowTileRequest> requests{
		{ 1, 512, 3, 9.0f }, // Three quadrants.
		{ 2, 512, 2, 8.0f }, // One quadrant left: halves to 256 x 2.
		{ 3, 256, 4, 7.0f }, // Half a quadrant left (2 x 256 cells): halves to 128 x 4.
		{ 4, 128, 4, 6.0f }, // Exactly the rest (4 x 128).
		{ 5, 128, 1, 5.0f }, // Full: evicted.
	};
	const auto allocations = atlas.Allocate(requests);
	SWIM_CHECK(Valid(allocations, 1024));
	SWIM_CHECK(allocations[0].Tiles.front().Size == 512 && !allocations[0].Downgraded);
	SWIM_CHECK(allocations[1].Tiles.size() == 2 && allocations[1].Tiles.front().Size == 256 && allocations[1].Downgraded);
	SWIM_CHECK(allocations[2].Tiles.size() == 4 && allocations[2].Tiles.front().Size == 128 && allocations[2].Downgraded);
	SWIM_CHECK(allocations[3].Tiles.size() == 4 && allocations[3].Tiles.front().Size == 128 && !allocations[3].Downgraded);
	SWIM_CHECK(allocations[4].Tiles.empty() && !allocations[4].Downgraded);
	const auto& stats = atlas.GetStats();
	SWIM_CHECK_EQUAL(stats.Placed, 4u);
	SWIM_CHECK_EQUAL(stats.Downgraded, 2u);
	SWIM_CHECK_EQUAL(stats.Evicted, 1u);
	SWIM_CHECK_EQUAL(stats.UsedTexels, std::uint64_t(1024) * 1024);
}

SWIM_TEST("Render.Shadows.Atlas", "NewHighPriorityLightsDisplaceTheLowestKeptTiles")
{
	ShadowAtlasAllocator atlas(1024, 256);
	std::vector<ShadowTileRequest> requests{ { 1, 512, 1, 4.0f }, { 2, 512, 1, 3.0f }, { 3, 512, 1, 2.0f }, { 4, 512, 1, 1.0f } };
	const auto first = atlas.Allocate(requests);
	SWIM_REQUIRE(Valid(first, 1024));

	// A full atlas and a new, most important 512 light: key 4 (lowest) gives up its
	// quadrant, then no longer fits anywhere and is evicted. The others do not move.
	requests.push_back({ 5, 512, 1, 9.0f });
	const auto second = atlas.Allocate(requests);
	SWIM_CHECK(Valid(second, 1024));
	SWIM_CHECK(second[4].Tiles == first[3].Tiles && !second[4].Reused);
	SWIM_CHECK(second[3].Tiles.empty());
	SWIM_CHECK(second[0].Reused && second[1].Reused && second[2].Reused);

	// A new light less important than every kept one does not displace anything.
	requests.back().Priority = 9.0f;
	requests.push_back({ 6, 256, 1, 0.5f });
	const auto third = atlas.Allocate(requests);
	SWIM_CHECK(third[5].Tiles.empty());
	SWIM_CHECK(third[0].Reused && third[1].Reused && third[2].Reused && third[4].Reused);
}

SWIM_TEST("Render.Shadows.Atlas", "DowngradedTilesAreKeptThenGrowBackWhenRoomFrees")
{
	ShadowAtlasAllocator atlas(1024, 128);
	std::vector<ShadowTileRequest> requests{ { 1, 512, 3, 9.0f }, { 2, 512, 2, 1.0f } };
	const auto first = atlas.Allocate(requests);
	SWIM_REQUIRE(first[1].Downgraded && first[1].Tiles.front().Size == 256);
	const auto second = atlas.Allocate(requests);
	SWIM_CHECK(second[1].Reused && second[1].Downgraded && second[1].Tiles == first[1].Tiles);
	SWIM_CHECK_EQUAL(atlas.GetStats().Downgraded, 1u);

	requests.erase(requests.begin());
	const auto third = atlas.Allocate(requests);
	SWIM_CHECK(!third[0].Reused && !third[0].Downgraded && third[0].Tiles.front().Size == 512);
	SWIM_CHECK(Valid(third, 1024));
	const auto fourth = atlas.Allocate(requests);
	SWIM_CHECK(fourth[0].Reused && fourth[0].Tiles == third[0].Tiles);
}

SWIM_TEST("Render.Shadows.Atlas", "GroupsArePlacedOrEvictedTogether")
{
	ShadowAtlasAllocator atlas(512, 256);
	// Four 256 cells. A point light needs six: it cannot fit even at MinTile and must
	// not leave partial tiles behind for the lower-priority spot.
	const std::vector<ShadowTileRequest> requests{ { 1, 256, 6, 9.0f }, { 2, 256, 4, 1.0f } };
	const auto allocations = atlas.Allocate(requests);
	SWIM_CHECK(allocations[0].Tiles.empty());
	SWIM_CHECK_EQUAL(allocations[1].Tiles.size(), std::size_t(4));
	SWIM_CHECK(Valid(allocations, 512));
}

SWIM_TEST("Render.Shadows.Atlas", "PriorityTiesBreakByKeyDeterministically")
{
	ShadowAtlasAllocator a(1024, 256);
	ShadowAtlasAllocator b(1024, 256);
	const std::vector<ShadowTileRequest> forward{ { 5, 512, 1, 1.0f }, { 2, 512, 1, 1.0f }, { 8, 512, 1, 1.0f } };
	const std::vector<ShadowTileRequest> backward{ { 8, 512, 1, 1.0f }, { 2, 512, 1, 1.0f }, { 5, 512, 1, 1.0f } };
	const auto first = a.Allocate(forward);
	const auto second = b.Allocate(backward);
	SWIM_CHECK(first[0].Tiles == second[2].Tiles);
	SWIM_CHECK(first[1].Tiles == second[1].Tiles);
	SWIM_CHECK(first[2].Tiles == second[0].Tiles);
	SWIM_CHECK((first[1].Tiles.front() == ShadowTile{ 0, 0, 512 })); // Key 2 first.
}

SWIM_TEST("Render.Shadows.Atlas", "RandomFramesStayValidAndMostlyStable")
{
	ShadowAtlasAllocator atlas(4096, 128);
	std::mt19937 random(71);
	std::uniform_int_distribution<int> sizeLog(7, 10);
	std::uniform_int_distribution<int> countPick(0, 2);
	std::uniform_real_distribution<float> priority(0.0f, 1.0f);
	std::vector<ShadowTileRequest> requests;
	for (std::uint64_t key = 0; key < 40; ++key)
	{
		const std::uint32_t counts[3]{ 1, 4, 6 };
		requests.push_back({ key, 1u << sizeLog(random), counts[countPick(random)], priority(random) });
	}
	std::uint32_t reused = 0;
	for (int frame = 0; frame < 20; ++frame)
	{
		// A few lights change size or priority each frame.
		for (int change = 0; change < 3; ++change)
		{
			auto& request = requests[std::size_t(random() % requests.size())];
			request.Priority = priority(random);
			if (random() % 2)
			{
				request.Size = 1u << sizeLog(random);
			}
		}
		const auto allocations = atlas.Allocate(requests);
		SWIM_CHECK(Valid(allocations, 4096));
		const auto& stats = atlas.GetStats();
		SWIM_CHECK_EQUAL(stats.Placed + stats.Evicted, stats.Requests);
		std::uint64_t used = 0;
		for (const auto& allocation : allocations)
		{
			used += std::uint64_t(allocation.Tiles.size()) * (allocation.Tiles.empty() ? 0 : allocation.Tiles.front().Size) *
				(allocation.Tiles.empty() ? 0 : allocation.Tiles.front().Size);
		}
		SWIM_CHECK_EQUAL(stats.UsedTexels, used);
		if (frame > 0)
		{
			reused += stats.Reused;
		}
	}
	// Most unchanged lights keep their tiles.
	SWIM_CHECK(reused > 19u * 20u);
}

SWIM_TEST("Render.Shadows.Atlas", "RejectsInvalidSizesCountsAndKeys")
{
	SWIM_CHECK_THROWS(ShadowAtlasAllocator(1000, 128), std::invalid_argument);
	SWIM_CHECK_THROWS(ShadowAtlasAllocator(1024, 100), std::invalid_argument);
	SWIM_CHECK_THROWS(ShadowAtlasAllocator(512, 1024), std::invalid_argument);
	ShadowAtlasAllocator atlas(1024, 128);
	const std::vector<ShadowTileRequest> badSize{ { 1, 300, 1, 0.0f } };
	const std::vector<ShadowTileRequest> tooSmall{ { 1, 64, 1, 0.0f } };
	const std::vector<ShadowTileRequest> tooLarge{ { 1, 2048, 1, 0.0f } };
	const std::vector<ShadowTileRequest> noCount{ { 1, 128, 0, 0.0f } };
	const std::vector<ShadowTileRequest> duplicate{ { 1, 128, 1, 0.0f }, { 1, 256, 1, 1.0f } };
	SWIM_CHECK_THROWS(atlas.Allocate(badSize), std::invalid_argument);
	SWIM_CHECK_THROWS(atlas.Allocate(tooSmall), std::invalid_argument);
	SWIM_CHECK_THROWS(atlas.Allocate(tooLarge), std::invalid_argument);
	SWIM_CHECK_THROWS(atlas.Allocate(noCount), std::invalid_argument);
	SWIM_CHECK_THROWS(atlas.Allocate(duplicate), std::invalid_argument);
	SWIM_CHECK(atlas.Allocate({}).empty());
}
