#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <random>
#include <set>

using namespace Swim;
using namespace Swim::Render;
namespace Cl = Swim::Render::Clustering;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	ClusterGridDesc Desc(std::uint32_t width = 1280, std::uint32_t height = 720)
	{
		ClusterGridDesc desc;
		desc.ViewportWidth = width;
		desc.ViewportHeight = height;
		desc.TileSize = 64;
		desc.SliceCount = 16;
		desc.Near = 0.1f;
		desc.Far = 60.0f;
		desc.MaxLightsPerCluster = 256;
		desc.LightCapacity = 1024;
		return desc;
	}

	ClusterGridRecord Grid(const ClusterGridDesc& desc)
	{
		return MakeClusterGridRecord(desc, Scene::Camera(float(desc.ViewportWidth) / float(desc.ViewportHeight)));
	}

	bool Contains(const Cl::Aabb& box, const Cl::Float3& p, float epsilon)
	{
		for (int c = 0; c < 3; ++c)
		{
			const float slack = epsilon * (1.0f + std::abs(p[c]));
			if (p[c] < box.Min[c] - slack || p[c] > box.Max[c] + slack)
			{
				return false;
			}
		}
		return true;
	}
} // namespace

SWIM_TEST("Render.ClusterGrid", "LayoutRoundsTilesUpAndValidatesTheDesc")
{
	auto desc = Desc(1920, 1080);
	auto layout = ComputeClusterGridLayout(desc);
	SWIM_CHECK_EQUAL(layout.TilesX, 30u);
	SWIM_CHECK_EQUAL(layout.TilesY, 17u); // 1080 / 64 = 16.9.
	SWIM_CHECK_EQUAL(layout.Slices, 16u);
	SWIM_CHECK_EQUAL(layout.ClusterCount, 30u * 17u * 16u);
	SWIM_CHECK(std::abs(layout.SliceScale - 16.0f / std::log(600.0f)) < 1.0e-5f);

	const auto rejects = [&](auto mutate)
	{
		auto bad = Desc();
		mutate(bad);
		SWIM_CHECK_THROWS(ComputeClusterGridLayout(bad), std::invalid_argument);
	};
	rejects(
		[](ClusterGridDesc& d)
		{
			d.ViewportWidth = 0;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.TileSize = 0;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.SliceCount = 0;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.Near = 0.0f;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.Far = d.Near;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.Far = INFINITY;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.MaxLightsPerCluster = 0;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.LightCapacity = 0;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.LightCapacity = MaxClusterLights + 1;
		});
	rejects(
		[](ClusterGridDesc& d)
		{
			d.TileSize = 1;
			d.ViewportWidth = 4096;
			d.ViewportHeight = 4096;
		}); // > MaxClusterCount.

	// Record packing: view rows, projection terms, slices, limits.
	const auto view = Scene::Camera(1280.0f / 720.0f);
	const auto record = MakeClusterGridRecord(desc, view);
	SWIM_CHECK_EQUAL(record.ViewRows[2][3], view.View[11]);
	SWIM_CHECK_EQUAL(record.Projection[0], view.Projection[0]);
	SWIM_CHECK_EQUAL(record.Projection[1], view.Projection[5]);
	SWIM_CHECK_EQUAL(record.DepthParams[1], view.Projection[11]);
	SWIM_CHECK_EQUAL(record.DepthParams[3], 60.0f);
	SWIM_CHECK_EQUAL(record.SliceParams[2], 64.0f);
	SWIM_CHECK_EQUAL(record.Dimensions[3], layout.ClusterCount);
	SWIM_CHECK_EQUAL(record.Limits[2], 256u);
	SWIM_CHECK_EQUAL(record.Limits[3], 32u); // 1024 lights = 32 mask words.
	SWIM_CHECK_EQUAL(ClusterOccupancyWords(record), 1u);
	SWIM_CHECK_EQUAL(ClusterBlockWords(record), 33u);
	auto orthographic = view;
	orthographic.Projection = OrthographicReverseZRowMajor(-1, 1, -1, 1, 0.1f, 10.0f);
	SWIM_CHECK_THROWS(MakeClusterGridRecord(desc, orthographic), std::invalid_argument);
	auto projective = view;
	projective.View[14] = 0.5f;
	SWIM_CHECK_THROWS(MakeClusterGridRecord(desc, projective), std::invalid_argument);

	// A resolution change regenerates the tiles; slicing is unchanged.
	desc.ViewportWidth = 640;
	desc.ViewportHeight = 360;
	const auto smaller = MakeClusterGridRecord(desc, Scene::Camera(640.0f / 360.0f));
	SWIM_CHECK(smaller.Dimensions[0] == 10u && smaller.Dimensions[1] == 6u);
	SWIM_CHECK_EQUAL(smaller.SliceParams[0], record.SliceParams[0]);
	SWIM_CHECK_EQUAL(smaller.Limits[0], 640u);
}

SWIM_TEST("Render.ClusterGrid", "SlicesAreLogarithmicAndDepthDecodesFromTheDepthBuffer")
{
	const auto grid = Grid(Desc());
	SWIM_CHECK(std::abs(ClusterSliceNearDepth(grid, 0) - 0.1f) < 1.0e-6f);
	SWIM_CHECK(std::abs(ClusterSliceNearDepth(grid, 16) - 60.0f) < 1.0e-3f);
	const float ratio = ClusterSliceNearDepth(grid, 1) / ClusterSliceNearDepth(grid, 0);
	for (std::uint32_t k = 1; k < 16; ++k)
	{
		const float near = ClusterSliceNearDepth(grid, k);
		SWIM_CHECK(std::abs(near / ClusterSliceNearDepth(grid, k - 1) - ratio) < 1.0e-4f * ratio); // Equal depth ratios.
		SWIM_CHECK_EQUAL(ClusterSliceForDepth(grid, near * 1.001f), k);
		SWIM_CHECK_EQUAL(ClusterSliceForDepth(grid, near * 0.999f), k - 1);
	}
	SWIM_CHECK_EQUAL(ClusterSliceForDepth(grid, 0.01f), 0u); // Nearer than Near: slice 0.
	SWIM_CHECK_EQUAL(ClusterSliceForDepth(grid, 0.0f), 0u);
	SWIM_CHECK_EQUAL(ClusterSliceForDepth(grid, 1000.0f), 15u); // Beyond Far: the last slice.

	// Depth-buffer decoding for reverse-Z infinite and forward finite projections.
	for (const float depth : { 0.2f, 1.0f, 7.5f, 59.0f })
	{
		const float reverse = grid.DepthParams[1] / depth; // near / d.
		SWIM_CHECK(std::abs(ClusterViewDepthFromNdc(grid, reverse) - depth) < 1.0e-4f * depth);
	}
	ClusterView forward = Scene::Camera(16.0f / 9.0f);
	forward.Projection = PerspectiveRowMajor(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
	const auto forwardGrid = MakeClusterGridRecord(Desc(), forward);
	for (const float depth : { 0.5f, 3.0f, 40.0f })
	{
		const float clipZ = forward.Projection[10] * -depth + forward.Projection[11];
		SWIM_CHECK(std::abs(ClusterViewDepthFromNdc(forwardGrid, clipZ / depth) - depth) < 1.0e-3f * depth);
	}
	SWIM_CHECK(!(ClusterViewDepthFromNdc(grid, 0.0f) <= 60.0f)); // The reverse-Z clear value is infinitely far.
}

SWIM_TEST("Render.ClusterGrid", "ClusterBoundsContainTheirPixelsAndTileTheVolume")
{
	const auto grid = Grid(Desc(1000, 600)); // Partial edge tiles in both axes.
	SWIM_CHECK(grid.Dimensions[0] == 16u && grid.Dimensions[1] == 10u);
	std::mt19937 random(64);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (int i = 0; i < 4000; ++i)
	{
		const float px = unit(random) * 1000.0f;
		const float py = unit(random) * 600.0f;
		const float depth = 0.02f + unit(random) * 59.9f;
		const auto cluster = ClusterIndexFor(grid, px, py, depth);
		SWIM_REQUIRE(cluster < grid.Dimensions[3]);
		SWIM_CHECK(Contains(Cl::ClusterBounds(grid, cluster), Scene::ViewPoint(grid, px, py, depth), 1.0e-4f));
		SWIM_CHECK(Contains(Cl::VolumeBounds(grid), Scene::ViewPoint(grid, px, py, depth), 1.0e-4f));
	}
	// Index layout: x fastest, then y (top row first), then slice.
	SWIM_CHECK_EQUAL(ClusterIndexFor(grid, 1.0f, 1.0f, 0.05f), 0u);
	SWIM_CHECK_EQUAL(ClusterIndexFor(grid, 999.0f, 1.0f, 0.05f), 15u);
	SWIM_CHECK_EQUAL(ClusterIndexFor(grid, 1.0f, 599.0f, 0.05f), 16u * 9u);
	SWIM_CHECK_EQUAL(ClusterIndexFor(grid, 1.0f, 1.0f, 100.0f), 16u * 10u * 15u);
	// Slice 0 reaches the eye; edge tiles end at the viewport edge.
	SWIM_CHECK(Cl::ClusterBounds(grid, 0).Max[2] == 0.0f);
	const auto edge = Cl::ClusterBounds(grid, 15);
	const auto rightEdge = Scene::ViewPoint(grid, 1000.0f, 300.0f, ClusterSliceNearDepth(grid, 1));
	SWIM_CHECK(std::abs(edge.Max[0] - std::max(rightEdge[0], 0.0f)) < 1.0e-4f);

	// Sphere/AABB tests.
	const Cl::Aabb box{ { 0, 0, 0 }, { 1, 1, 1 } };
	SWIM_CHECK(Cl::SphereIntersectsAabb({ 0.5f, 0.5f, 0.5f }, 0.0f, box));
	SWIM_CHECK(Cl::SphereIntersectsAabb({ 2, 0.5f, 0.5f }, 1.0f, box));
	SWIM_CHECK(!Cl::SphereIntersectsAabb({ 2, 2, 2 }, 1.7f, box)); // Corner distance sqrt(3) > 1.7.
	SWIM_CHECK(Cl::SphereIntersectsAabb({ 2, 2, 2 }, 1.74f, box));
	SWIM_CHECK(!Cl::SphereIntersectsAabb({ 0.5f, 0.5f, 0.5f }, -1.0f, box)); // Culled lights never match.
	SWIM_CHECK_EQUAL(Cl::DistanceSquaredToAabb({ 3, 0.5f, -1 }, box), 5.0f);
}

SWIM_TEST("Render.ClusteredLights", "AssignmentIsConservativeAndClusteredShadingEqualsBruteForce")
{
	const auto grid = Grid(Desc());
	const auto scene = Scene::RandomScene(2, 600, 65);
	const auto assignment = Cl::AssignLights(grid, scene.Rows, scene.Header);
	SWIM_CHECK_EQUAL(assignment.Stats.DroppedIndices, 0u);
	SWIM_CHECK_EQUAL(assignment.Stats.OverflowClusters, 0u);
	SWIM_CHECK(assignment.Stats.VisibleLights > 100u && assignment.Stats.VisibleLights < 600u); // Some lie outside.
	SWIM_CHECK(assignment.Stats.NonEmptyClusters > 100u);
	SWIM_CHECK_EQUAL(assignment.Stats.ClusterCount, grid.Dimensions[3]);

	// Culling is exact against the whole volume: a culled light's sphere misses it.
	for (std::uint32_t i = 0; i < scene.Header.LocalCount; ++i)
	{
		const auto sphere = Lights::LightBoundingSphere(scene.Rows[scene.Header.FirstLocalRow + i]);
		const bool touches = Cl::SphereIntersectsAabb(Cl::WorldToView(grid, sphere.Center), sphere.Radius, Cl::VolumeBounds(grid));
		SWIM_CHECK(touches == (assignment.ViewLights[i].Radius >= 0.0f));
	}

	// Conservative: every light lighting a point is in the point's cluster list, and
	// clustered shading reproduces the brute-force sum.
	std::mt19937 random(66);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::uint32_t litPoints = 0;
	for (int i = 0; i < 3000; ++i)
	{
		const float px = unit(random) * 1280.0f;
		const float py = unit(random) * 720.0f;
		const float depth = 0.05f + unit(random) * 40.0f;
		const auto world = Scene::ViewToWorld(grid, Scene::ViewPoint(grid, px, py, depth));
		const auto lights = Cl::ClusterLightList(assignment, grid, ClusterIndexFor(grid, px, py, depth));
		const std::set<std::uint32_t> list(lights.begin(), lights.end());
		bool lit = false;
		for (std::uint32_t light = 0; light < scene.Header.LocalCount; ++light)
		{
			if (Lights::EvaluateLight(scene.Rows[scene.Header.FirstLocalRow + light], world).Radiance[0] > 0.0f)
			{
				SWIM_CHECK(list.count(light) == 1);
				lit = true;
			}
		}
		litPoints += lit ? 1u : 0u;
		StandardPbr::Surface surface;
		surface.BaseColor = { 0.7f, 0.5f, 0.3f };
		surface.Metallic = unit(random);
		surface.PerceptualRoughness = 0.2f + 0.8f * unit(random);
		const Cl::Float3 normal = StandardPbr::Normalize({ unit(random) - 0.5f, 1.0f, unit(random) - 0.5f });
		const Cl::Float3 view = StandardPbr::Normalize({ unit(random) - 0.5f, 0.5f, 1.0f });
		const auto brute = Lights::ShadeAllLights(scene.Rows, scene.Header, surface, normal, view, world);
		const auto clustered = Cl::ShadeClustered(assignment, grid, scene.Rows, scene.Header, surface, normal, view, world, px, py, depth);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(clustered[c] - brute[c]) <= 1.0e-5f + 1.0e-4f * std::abs(brute[c]));
		}
	}
	SWIM_CHECK(litPoints > 300u);

	// Each cluster owns one block (occupancy words, then mask words); the occupancy bits
	// mark exactly the non-zero mask words, and the counts are the set bits.
	const std::uint32_t block = ClusterBlockWords(grid);
	const std::uint32_t occupancyWords = ClusterOccupancyWords(grid);
	SWIM_CHECK_EQUAL(assignment.Indices.size(), std::size_t(grid.Dimensions[3]) * block);
	std::uint32_t total = 0;
	for (std::uint32_t c = 0; c < grid.Dimensions[3]; ++c)
	{
		const auto& record = assignment.Records[c];
		SWIM_CHECK_EQUAL(record.Offset, c * block);
		SWIM_CHECK_EQUAL(record.Count, record.RawCount);
		std::uint32_t bits = 0;
		for (std::uint32_t w = 0; w < ClusterMaskWords(grid); ++w)
		{
			const std::uint32_t mask = assignment.Indices[record.Offset + occupancyWords + w];
			bits += static_cast<std::uint32_t>(std::popcount(mask));
			const bool occupied = (assignment.Indices[record.Offset + w / 32] >> (w % 32) & 1u) != 0;
			SWIM_CHECK(occupied == (mask != 0));
		}
		SWIM_CHECK_EQUAL(bits, record.Count);
		const auto list = Cl::ClusterLightList(assignment, grid, c);
		SWIM_CHECK_EQUAL(std::uint32_t(list.size()), record.Count);
		SWIM_CHECK(std::is_sorted(list.begin(), list.end()));
		total += record.Count;
	}
	SWIM_CHECK_EQUAL(total, assignment.Stats.WrittenIndices);
	SWIM_CHECK_EQUAL(assignment.Stats.RequestedIndices, assignment.Stats.WrittenIndices);

	// No lights: empty lists, zero stats.
	Scene::Scene empty = Scene::RandomScene(1, 0, 67);
	const auto none = Cl::AssignLights(grid, empty.Rows, empty.Header);
	SWIM_CHECK_EQUAL(none.Stats.WrittenIndices, 0u);
	SWIM_CHECK_EQUAL(none.Stats.NonEmptyClusters, 0u);
	SWIM_CHECK_EQUAL(none.Stats.VisibleLights, 0u);
}

SWIM_TEST("Render.ClusteredLights", "DenseSwarmsAreNeverTruncated")
{
	// Thousands of overlapping lights seen from afar: every cluster keeps every light
	// touching it, whatever MaxLightsPerCluster (the heatmap scale) says. Truncated
	// lists were what showed as square, darker tiles over big light swarms.
	auto desc = Desc(640, 360);
	desc.MaxLightsPerCluster = 4;
	desc.LightCapacity = 4096;
	const auto grid = Grid(desc);
	const auto scene = Scene::RandomScene(0, 3000, 68, 4.0f, 10.0f);
	const auto assignment = Cl::AssignLights(grid, scene.Rows, scene.Header);
	SWIM_CHECK_EQUAL(assignment.Stats.DroppedIndices, 0u);
	SWIM_CHECK(assignment.Stats.MaxRawLightsPerCluster > 256u); // Far more than the old per-cluster cap.
	SWIM_CHECK(assignment.Stats.OverflowClusters > 0u);			// Above the heatmap scale, nothing dropped.
	std::uint32_t checked = 0;
	for (std::uint32_t c = 0; c < grid.Dimensions[3]; c += 7)
	{
		std::vector<std::uint32_t> expected;
		for (std::uint32_t i = 0; i < scene.Header.LocalCount; ++i)
		{
			const auto& light = assignment.ViewLights[i];
			if (Cl::SphereIntersectsAabb(light.Center, light.Radius, assignment.Bounds[c]))
			{
				expected.push_back(i);
			}
		}
		SWIM_CHECK(Cl::ClusterLightList(assignment, grid, c) == expected);
		checked += expected.empty() ? 0u : 1u;
	}
	SWIM_CHECK(checked > 50u);

	// More lights than the masks address is rejected instead of silently dropped.
	desc.LightCapacity = 1024;
	SWIM_CHECK_THROWS(Cl::AssignLights(Grid(desc), scene.Rows, scene.Header), std::invalid_argument);
}

SWIM_TEST("Render.ClusteredLights", "HeatmapColorsCountsAndFlagsTruncation")
{
	SWIM_CHECK((Cl::HeatmapColor(0, 0, 32) == std::array<float, 4>{ 0, 0, 0, 0 }));
	SWIM_CHECK((Cl::HeatmapColor(32, 32, 32) == std::array<float, 4>{ 1, 0, 0, 1 }));
	SWIM_CHECK((Cl::HeatmapColor(16, 16, 32) == std::array<float, 4>{ 0, 1, 0, 1 }));
	SWIM_CHECK((Cl::HeatmapColor(4, 40, 4) == std::array<float, 4>{ 1, 0, 1, 1 })); // Truncated.
	const auto low = Cl::HeatmapColor(1, 1, 32);
	SWIM_CHECK(low[2] > 0.9f && low[1] < 0.1f && low[3] == 1.0f);

	const auto grid = Grid(Desc(320, 180));
	const auto scene = Scene::RandomScene(0, 200, 69);
	const auto assignment = Cl::AssignLights(grid, scene.Rows, scene.Header);
	// Far pixels (reverse-Z clear value 0) are transparent; others use their cluster.
	SWIM_CHECK((Cl::HeatmapPixel(grid, assignment.Records, 10.5f, 10.5f, 0.0f) == std::array<float, 4>{ 0, 0, 0, 0 }));
	const float depth = 8.0f;
	const float ndc = grid.DepthParams[1] / depth;
	const auto& record = assignment.Records[ClusterIndexFor(grid, 100.5f, 50.5f, depth)];
	SWIM_CHECK((Cl::HeatmapPixel(grid, assignment.Records, 100.5f, 50.5f, ndc) == Cl::HeatmapColor(record.Count, record.RawCount, 256)));
}
