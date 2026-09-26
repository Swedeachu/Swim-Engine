#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace Swim;
using namespace Swim::Render;
namespace Cl = Swim::Render::Clustering;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	enum class Layout
	{
		Uniform,   // Scattered through the view (ClusterScene::RandomScene's box).
		OffScreen, // Behind the camera, out of reach of the clustered volume.
		Dense,	   // Packed into a 4 m ball in front of the camera, large ranges.
	};

	Scene::Scene MakeScene(Layout layout, std::uint32_t count, std::uint32_t seed)
	{
		auto scene = Scene::RandomScene(1, count, seed);
		std::mt19937 random(seed + 1);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			auto desc = scene.Descs[i];
			if (layout == Layout::OffScreen)
			{
				desc.Position = { 40 * unit(random) - 20, 12 * unit(random) - 2,
					30.0f + 20.0f * unit(random) }; // Camera at z = 18 looks at -z.
			}
			else if (layout == Layout::Dense)
			{
				desc.Position = { 4 * unit(random) - 2, 2 + 4 * unit(random) - 2, 4 * unit(random) - 2 };
				desc.Range = 3.0f + 3.0f * unit(random);
			}
			scene.Descs[i] = desc;
			scene.Rows[scene.Header.FirstLocalRow + i] = Lights::EncodeLight(desc);
		}
		return scene;
	}

	// Every ClusterStats field against its definition over the assignment's own records.
	void CheckStats(const ClusterGridRecord& grid, const Cl::ClusterAssignment& assignment, std::uint32_t visibleExpected)
	{
		const auto maxPerCluster = grid.Limits[2];
		std::uint64_t requested = 0;
		std::uint32_t overflow = 0, nonEmpty = 0, maxRaw = 0, written = 0;
		for (const auto& record : assignment.Records)
		{
			requested += record.RawCount;
			overflow += record.RawCount > maxPerCluster ? 1u : 0u;
			nonEmpty += record.Count > 0 ? 1u : 0u;
			maxRaw = std::max(maxRaw, record.RawCount);
			written += record.Count;
			SWIM_CHECK_EQUAL(record.Count, record.RawCount); // Never truncated.
		}
		const auto& stats = assignment.Stats;
		SWIM_CHECK_EQUAL(stats.VisibleLights, visibleExpected);
		SWIM_CHECK_EQUAL(std::uint64_t(stats.RequestedIndices), requested);
		SWIM_CHECK_EQUAL(stats.WrittenIndices, written);
		SWIM_CHECK_EQUAL(stats.DroppedIndices, 0u);
		SWIM_CHECK_EQUAL(stats.OverflowClusters, overflow);
		SWIM_CHECK_EQUAL(stats.NonEmptyClusters, nonEmpty);
		SWIM_CHECK_EQUAL(stats.MaxRawLightsPerCluster, maxRaw);
		SWIM_CHECK_EQUAL(stats.ClusterCount, std::uint32_t(assignment.Records.size()));
	}
} // namespace

// Item 69 on the CPU reference: the scaling scenarios the native benchmark times
// (Phase 15: 1k and 10k lights, mostly off-screen, dense overlap, no lights) keep
// every statistic consistent, and no cluster ever drops a light. The native
// ClusteredLightingScalesToTensOfThousandsOfLights smoke times the GPU passes.
SWIM_TEST("Render.ClusteredLights", "ScalingScenariosKeepStatisticsConsistentAndBounded")
{
	ClusterGridDesc desc;
	desc.ViewportWidth = 640;
	desc.ViewportHeight = 360;
	desc.TileSize = 64;
	desc.SliceCount = 16;
	desc.Far = 60.0f;
	desc.MaxLightsPerCluster = 128;
	desc.LightCapacity = 10000;
	const auto grid = MakeClusterGridRecord(desc, Scene::Camera(16.0f / 9.0f));

	// No lights: the empty fast path writes nothing.
	{
		auto empty = Scene::RandomScene(1, 0, 1);
		const auto assignment = Cl::AssignLights(grid, empty.Rows, empty.Header);
		CheckStats(grid, assignment, 0);
		SWIM_CHECK_EQUAL(assignment.Stats.RequestedIndices, 0u);
		SWIM_CHECK_EQUAL(assignment.Stats.NonEmptyClusters, 0u);
		SWIM_CHECK(std::all_of(assignment.Indices.begin(), assignment.Indices.end(),
			[](std::uint32_t w)
			{
				return w == 0;
			}));
	}

	struct Case
	{
		const char* Name;
		Layout Shape;
		std::uint32_t Count;
	};

	for (const auto& c : { Case{ "uniform", Layout::Uniform, 1000 }, Case{ "uniform", Layout::Uniform, 10000 },
			 Case{ "off-screen", Layout::OffScreen, 10000 }, Case{ "dense", Layout::Dense, 1000 } })
	{
		const auto scene = MakeScene(c.Shape, c.Count, 690 + c.Count);
		std::uint32_t visible = 0;
		for (std::uint32_t i = 0; i < c.Count; ++i)
		{
			visible += Cl::CullLight(grid, scene.Rows[scene.Header.FirstLocalRow + i]).Radius >= 0.0f ? 1u : 0u;
		}
		const auto start = std::chrono::steady_clock::now();
		const auto assignment = Cl::AssignLights(grid, scene.Rows, scene.Header);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		CheckStats(grid, assignment, visible);
		const auto& stats = assignment.Stats;
		if (c.Shape == Layout::OffScreen)
		{
			SWIM_CHECK_EQUAL(stats.VisibleLights, 0u); // Culled before any cluster test.
			SWIM_CHECK_EQUAL(stats.RequestedIndices, 0u);
		}
		if (c.Shape == Layout::Dense)
		{
			SWIM_CHECK(stats.OverflowClusters > 0u); // Above the heatmap scale; nothing dropped.
			SWIM_CHECK(stats.MaxRawLightsPerCluster > desc.MaxLightsPerCluster);
			SWIM_CHECK_EQUAL(stats.VisibleLights, c.Count);
		}
		if (c.Shape == Layout::Uniform)
		{
			SWIM_CHECK(stats.VisibleLights > c.Count / 2);
			// 10k lights exceed the heatmap scale in places; every light is still kept.
			SWIM_CHECK((stats.OverflowClusters == 0u) == (c.Count == 1000));
		}
		std::printf("             [clusters CPU reference] %-10s %5u lights: %5u visible, %6u indices, %4u overflowing, max %4u; %.2f ms\n",
			c.Name, c.Count, stats.VisibleLights, stats.WrittenIndices, stats.OverflowClusters, stats.MaxRawLightsPerCluster, ms);
	}
}
