#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityMath.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	constexpr auto Drawable = RenderObjectFlags::Live | RenderObjectFlags::HasMesh | RenderObjectFlags::Visible;

	// Camera at (0, 0, 10) looking down -Z with a 20x20 orthographic window.
	GpuViewRecord OrthoView(float lodScale = 1.0f, float pixelError = 1.0f, std::uint32_t flags = 0)
	{
		RenderViewDesc desc;
		const std::array<float, 16> view{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -10, 0, 0, 0, 1 };
		desc.ViewProjection = MultiplyRowMajor(OrthographicRowMajor(-10, 10, -10, 10, 0.1f, 100.0f), view);
		desc.CameraPosition = { 0, 0, 10 };
		desc.LodScale = lodScale;
		desc.LodPixelError = pixelError;
		desc.LodHysteresis = 0.25f;
		desc.Flags = flags;
		return BuildGpuViewRecord(desc);
	}

	struct Scene
	{
		std::vector<GpuInstanceRecord> Instances;
		std::vector<GpuTransformRecord> Transforms;
		std::vector<GpuMeshMetadata> Meshes;
		std::vector<GpuSubmeshRecord> Submeshes;

		// A mesh with `lods` LODs of one submesh each; LOD i has error errors[i].
		std::uint32_t AddMesh(std::vector<float> errors, std::uint32_t indexPage = 0, bool explicitLods = true)
		{
			GpuMeshMetadata mesh;
			mesh.IndexPage = indexPage;
			mesh.FirstSubmesh = static_cast<std::uint32_t>(Submeshes.size());
			mesh.SubmeshCount = static_cast<std::uint32_t>(errors.size());
			mesh.LodCount = explicitLods ? static_cast<std::uint32_t>(errors.size()) : 0;
			mesh.Generation = 1;
			for (std::uint32_t i = 0; i < errors.size(); ++i)
			{
				mesh.Lods[i] = { mesh.FirstSubmesh + i, 1, errors[i], 0 };
				Submeshes.push_back({ 100 * (i + 1), 3 * (i + 1), std::int32_t(10 * i), i });
			}
			Meshes.push_back(mesh);
			return static_cast<std::uint32_t>(Meshes.size() - 1);
		}

		std::uint32_t Add(
			std::uint32_t mesh, float x, float y, std::uint32_t materialSet = 0, RenderObjectFlags flags = Drawable, float extent = 1.0f)
		{
			GpuInstanceRecord instance;
			const auto row = static_cast<std::uint32_t>(Instances.size());
			instance.MeshIndex = mesh;
			instance.TransformIndex = row;
			instance.MaterialSet = materialSet;
			instance.ObjectId = row;
			instance.Flags = static_cast<std::uint32_t>(flags);
			instance.Generation = 1;
			instance.LocalExtents[0] = instance.LocalExtents[1] = instance.LocalExtents[2] = extent;
			Instances.push_back(instance);
			GpuTransformRecord transform;
			transform.Current[3] = x;
			transform.Current[7] = y;
			Transforms.push_back(transform);
			return row;
		}

		VisibilityReferenceResult Run(const GpuViewRecord& view, const VisibilityBinLayout& bins, std::vector<GpuLodState>& lods,
			std::vector<std::uint32_t> materialBins = { 0, 1 }, std::vector<std::uint32_t> pages = { 0 })
		{
			VisibilityReferenceInputs inputs{ Instances, Transforms, Meshes, Submeshes, view, materialBins, pages, &bins };
			return RunVisibilityReference(inputs, lods);
		}
	};

	std::uint32_t Total(const VisibilityReferenceResult& result)
	{
		std::uint32_t draws = 0;
		for (const auto& bin : result.Bins)
		{
			draws += static_cast<std::uint32_t>(bin.size());
		}
		return draws;
	}
} // namespace

SWIM_TEST("Render.Visibility", "ViewPlanesComeFromTheViewProjectionForBothProjections")
{
	const auto ortho = OrthoView();
	GpuInstanceRecord instance;
	GpuTransformRecord transform;
	const auto at = [&](float x, float y, float z, float radius)
	{
		instance.LocalExtents[0] = instance.LocalExtents[1] = instance.LocalExtents[2] = radius / std::sqrt(3.0f);
		transform.Current[3] = x;
		transform.Current[7] = y;
		transform.Current[11] = z;
		return VisibilityMath::WorldSphere(instance, transform);
	};
	SWIM_CHECK(VisibilityMath::InsideFrustum(ortho, at(0, 0, 0, 0.5f)));
	SWIM_CHECK(!VisibilityMath::InsideFrustum(ortho, at(12, 0, 0, 1.0f)));	 // Right of x = 10.
	SWIM_CHECK(VisibilityMath::InsideFrustum(ortho, at(10.5f, 0, 0, 1.0f))); // Straddles the edge.
	SWIM_CHECK(!VisibilityMath::InsideFrustum(ortho, at(0, -12, 0, 1.0f)));
	SWIM_CHECK(!VisibilityMath::InsideFrustum(ortho, at(0, 0, 11, 0.5f)));	// Behind the camera (near plane).
	SWIM_CHECK(!VisibilityMath::InsideFrustum(ortho, at(0, 0, -95, 1.0f))); // Beyond the far plane.
	// The near plane's normal points down -Z in world space.
	SWIM_CHECK(std::abs(ortho.FrustumPlanes[4 * 4 + 2] + 1.0f) < 1.0e-5f);

	RenderViewDesc perspective;
	perspective.ViewProjection = PerspectiveRowMajor(1.5707964f, 1.0f, 0.1f, 50.0f); // Camera at the origin.
	const auto view = BuildGpuViewRecord(perspective);
	SWIM_CHECK(VisibilityMath::InsideFrustum(view, at(0, 0, -5, 0.1f)));
	SWIM_CHECK(!VisibilityMath::InsideFrustum(view, at(0, 0, 5, 0.1f)));
	SWIM_CHECK(!VisibilityMath::InsideFrustum(view, at(8, 0, -5, 0.5f))); // Outside the 90 degree cone.
	SWIM_CHECK(VisibilityMath::InsideFrustum(view, at(4.8f, 0, -5, 0.5f)));

	// A degenerate plane (row 3 == row 2, as an infinite far plane) never culls.
	RenderViewDesc degenerate;
	degenerate.ViewProjection = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1 };
	const auto open = BuildGpuViewRecord(degenerate);
	SWIM_CHECK_EQUAL(open.FrustumPlanes[5 * 4 + 3], 1.0f);
	SWIM_CHECK_EQUAL(open.FrustumPlanes[5 * 4 + 0], 0.0f);
}

SWIM_TEST("Render.Visibility", "CullingCountsLiveRowsAndSkipsUndrawableOnes")
{
	Scene scene;
	const auto mesh = scene.AddMesh({ 0.0f });
	scene.Add(mesh, 0, 0);
	scene.Add(mesh, 30, 0);																	// Outside.
	scene.Add(mesh, 2, 2, 0, RenderObjectFlags::Live | RenderObjectFlags::HasMesh);			// Hidden.
	scene.Add(mesh, 3, 3, 0, RenderObjectFlags::Live | RenderObjectFlags::Visible);			// No mesh.
	scene.Add(mesh, 4, 4, 0, RenderObjectFlags::None);										// Dead row.
	scene.Add(mesh, 500, 0, 0, Drawable, RenderBounds::Unbounded);							// Unbounded: never culled.
	scene.Instances.back().LocalExtents[1] = scene.Instances.back().LocalExtents[2] = 0.0f; // Any huge axis counts.
	const VisibilityBinLayout bins(std::vector<std::uint32_t>{ 16, 16 }, 1);
	std::vector<GpuLodState> lods;
	auto result = scene.Run(OrthoView(), bins, lods);
	SWIM_CHECK_EQUAL(result.Stats.Tested, 5u);
	SWIM_CHECK_EQUAL(result.Stats.NotDrawable, 2u);
	SWIM_CHECK_EQUAL(result.Stats.FrustumCulled, 1u);
	SWIM_CHECK_EQUAL(result.Stats.Visible, 2u);
	SWIM_CHECK_EQUAL(result.Stats.Draws, 2u);
	SWIM_REQUIRE_EQUAL(result.Bins[0].size(), 2u);
	SWIM_CHECK_EQUAL(result.Bins[0][0].Record.InstanceRow, 0u);
	SWIM_CHECK_EQUAL(result.Bins[0][1].Record.InstanceRow, 5u);

	// Culling can be disabled per view.
	result = scene.Run(OrthoView(1, 1, std::uint32_t(GpuViewFlags::DisableFrustumCulling)), bins, lods);
	SWIM_CHECK_EQUAL(result.Stats.FrustumCulled, 0u);
	SWIM_CHECK_EQUAL(result.Stats.Visible, 3u);
}

SWIM_TEST("Render.Visibility", "ShadowCasterViewsSkipRowsThatDoNotCastShadows")
{
	Scene scene;
	const auto mesh = scene.AddMesh({ 0.0f });
	scene.Add(mesh, 0, 0, 0, Drawable | RenderObjectFlags::CastShadows);
	scene.Add(mesh, 2, 0, 0, Drawable); // Visible, but casts no shadow.
	scene.Add(mesh, 4, 0, 0, RenderObjectFlags::Live | RenderObjectFlags::HasMesh | RenderObjectFlags::CastShadows); // Hidden caster.
	scene.Add(mesh, -4, 0, 0, Drawable | RenderObjectFlags::CastShadows);
	const VisibilityBinLayout bins(std::vector<std::uint32_t>{ 16, 16 }, 1);
	std::vector<GpuLodState> lods;

	auto result = scene.Run(OrthoView(), bins, lods);
	SWIM_CHECK_EQUAL(result.Stats.Visible, 3u);
	SWIM_CHECK_EQUAL(result.Stats.NotDrawable, 1u);

	// Shadow views draw Visible rows that also cast (hidden rows stay hidden).
	result = scene.Run(OrthoView(1, 1, std::uint32_t(GpuViewFlags::ShadowCasters)), bins, lods);
	SWIM_CHECK_EQUAL(result.Stats.Tested, 4u);
	SWIM_CHECK_EQUAL(result.Stats.NotDrawable, 2u);
	SWIM_CHECK_EQUAL(result.Stats.Visible, 2u);
	SWIM_REQUIRE_EQUAL(result.Bins[0].size(), 2u);
	SWIM_CHECK_EQUAL(result.Bins[0][0].Record.InstanceRow, 0u);
	SWIM_CHECK_EQUAL(result.Bins[0][1].Record.InstanceRow, 3u);
}

SWIM_TEST("Render.Visibility", "LodFollowsProjectedErrorWithHysteresisAndHistoryResets")
{
	Scene scene;
	const auto mesh = scene.AddMesh({ 0.0f, 1.0f, 4.0f });
	const auto row = scene.Add(mesh, 0, 0, 0, Drawable, 0.0f); // A point: distance = 10 from the camera.
	const VisibilityBinLayout bins(std::vector<std::uint32_t>{ 16, 16 }, 1);
	std::vector<GpuLodState> lods;
	// errorScale = LodScale / 10. LOD1 fits when LodScale / 10 <= 1, LOD2 when 4 * LodScale / 10 <= 1.
	const auto lodAt = [&](float lodScale, std::uint32_t flags = 0)
	{
		const auto result = scene.Run(OrthoView(lodScale, 1.0f, flags), bins, lods);
		return result.Bins[0].at(0).Record.SubmeshRow - scene.Meshes[mesh].FirstSubmesh;
	};
	SWIM_CHECK_EQUAL(lodAt(20.0f), 0u); // No history: plain selection (LOD1 would be 2 px).
	SWIM_CHECK_EQUAL(lodAt(9.0f), 0u);	// LOD1 is 0.9 px, but coarsening needs <= 0.75 px: stays at LOD0.
	SWIM_CHECK_EQUAL(lodAt(5.0f), 1u);	// 0.5 px: coarsens.
	SWIM_CHECK_EQUAL(lods[row].Lod, 1u);
	SWIM_CHECK_EQUAL(lodAt(11.0f), 1u); // 1.1 px is inside the refine band (<= 1.25): stays at LOD1.
	SWIM_CHECK_EQUAL(lodAt(13.0f), 0u); // 1.3 px: refines.
	SWIM_CHECK_EQUAL(lodAt(1.5f), 2u);	// 0.6 px at LOD2: coarsens straight past LOD1.
	// A camera cut ignores history.
	SWIM_CHECK_EQUAL(lodAt(11.0f, std::uint32_t(GpuViewFlags::ResetLodHistory)), 0u);
	SWIM_CHECK_EQUAL(lodAt(2.0f), 1u); // From LOD0: LOD2 (0.8 px) is outside the coarsening band, LOD1 (0.2 px) is not.
	// A reused row (new generation) starts without history.
	scene.Instances[row].Generation = 2;
	SWIM_CHECK_EQUAL(lodAt(11.0f), 0u);
	SWIM_CHECK_EQUAL(lods[row].Generation, 2u);
	// LOD bias doubles the allowed error per step.
	scene.Instances[row].LodBias = 1.0f;
	std::vector<GpuLodState> fresh;
	const auto biased = scene.Run(OrthoView(15.0f), bins, fresh); // 1.5 px error at LOD1, allowed 2 px.
	SWIM_CHECK_EQUAL(biased.Bins[0][0].Record.SubmeshRow - scene.Meshes[mesh].FirstSubmesh, 1u);
	SWIM_CHECK_EQUAL(biased.Stats.LodCounts[1], 1u);
}

SWIM_TEST("Render.Visibility", "DrawsAreBinnedByMaterialAndIndexPageWithBoundedCapacity")
{
	Scene scene;
	const auto pageZero = scene.AddMesh({ 0.0f });
	const auto pageThree = scene.AddMesh({ 0.0f }, 3);
	const auto pageNine = scene.AddMesh({ 0.0f }, 9);
	const auto multi = scene.AddMesh({ 0.0f, 0.0f }, 0, false); // LodCount 0: every submesh draws.
	for (int i = 0; i < 4; ++i)
	{
		scene.Add(pageZero, float(i), 0, 0);
	}
	scene.Add(pageZero, 0, 1, 1);  // Material bin 1.
	scene.Add(pageThree, 1, 1, 1); // Bin 1, page slot 1.
	scene.Add(pageNine, 2, 1, 0);  // Page not listed.
	scene.Add(pageZero, 3, 1, 42); // Unmapped material set -> bin 0.
	scene.Add(multi, 4, 1, 1);	   // Two submeshes.
	const VisibilityBinLayout bins(std::vector<std::uint32_t>{ 3, 8 }, 2);
	SWIM_CHECK_EQUAL(bins.GetBinCount(), 4u);
	SWIM_CHECK_EQUAL(bins.GetTotalCapacity(), 22u);
	SWIM_CHECK_EQUAL(bins.GetRange(bins.GetBin(1, 0)).First, 6u);
	SWIM_CHECK_EQUAL(bins.GetRange(bins.GetBin(1, 1)).Capacity, 8u);

	std::vector<GpuLodState> lods;
	const auto result = scene.Run(OrthoView(), bins, lods, { 0, 1 }, { 0, 3 });
	// Bin 0 / slot 0 holds five candidates (4 + the unmapped one) but only 3 fit.
	SWIM_CHECK_EQUAL(result.Bins[bins.GetBin(0, 0)].size(), 3u);
	SWIM_CHECK_EQUAL(result.Stats.Dropped, 2u);
	SWIM_CHECK_EQUAL(result.Bins[bins.GetBin(1, 0)].size(), 3u); // Material 1 on page 0: one + two submeshes.
	SWIM_CHECK_EQUAL(result.Bins[bins.GetBin(1, 1)].size(), 1u);
	SWIM_CHECK(result.Bins[bins.GetBin(0, 1)].empty());
	SWIM_CHECK_EQUAL(result.Stats.OtherPage, 1u);
	SWIM_CHECK_EQUAL(result.Stats.Draws, Total(result));
	SWIM_CHECK_EQUAL(result.Stats.Visible, 9u);

	// Commands carry the submesh draw range; FirstInstance is the slot within the bin.
	const auto& draws = result.Bins[bins.GetBin(1, 0)];
	for (std::uint32_t slot = 0; slot < draws.size(); ++slot)
	{
		const auto& draw = draws[slot];
		const auto& submesh = scene.Submeshes[draw.Record.SubmeshRow];
		SWIM_CHECK_EQUAL(draw.Command.FirstInstance, slot);
		SWIM_CHECK_EQUAL(draw.Command.InstanceCount, 1u);
		SWIM_CHECK_EQUAL(draw.Command.IndexCount, submesh.IndexCount);
		SWIM_CHECK_EQUAL(draw.Command.FirstIndex, submesh.FirstIndex);
		SWIM_CHECK_EQUAL(draw.Command.VertexOffset, submesh.VertexOffset);
	}
	SWIM_CHECK_EQUAL(draws[1].Record.SubmeshRow, scene.Meshes[multi].FirstSubmesh);
	SWIM_CHECK_EQUAL(draws[2].Record.SubmeshRow, scene.Meshes[multi].FirstSubmesh + 1);
	SWIM_CHECK_THROWS(VisibilityBinLayout(std::vector<std::uint32_t>{}, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(VisibilityBinLayout(std::vector<std::uint32_t>{ 4 }, 0), std::invalid_argument);
}

SWIM_TEST("Render.Visibility", "HundredThousandObjectBenchmarkKeepsStatisticsConsistent")
{
	// Item 57 benchmark: the CPU definition over 100k GPU Scene rows. Every row is
	// accounted for exactly once, bins clamp with Dropped, and a repeated frame
	// with unchanged history is identical.
	Scene scene;
	const auto mesh = scene.AddMesh({ 0.0f, 1.0f, 4.0f });
	constexpr std::uint32_t width = 400;
	constexpr std::uint32_t height = 250;
	for (std::uint32_t j = 0; j < height; ++j)
	{
		for (std::uint32_t i = 0; i < width; ++i)
		{
			const auto row = j * width + i;
			const auto flags = row % 97 == 0 ? RenderObjectFlags::Live | RenderObjectFlags::HasMesh : Drawable;
			scene.Add(mesh, float(i) - 200.0f, float(j) - 125.0f, row % 7 == 0 ? 1u : 0u, flags, 0.4f);
		}
	}
	const VisibilityBinLayout bins(std::vector<std::uint32_t>{ 100000, 16 }, 1);
	const auto view = OrthoView(4.0f);
	std::vector<GpuLodState> lods;
	const auto start = std::chrono::steady_clock::now();
	const auto first = scene.Run(view, bins, lods);
	const auto middle = std::chrono::steady_clock::now();
	const auto second = scene.Run(view, bins, lods);
	const auto end = std::chrono::steady_clock::now();

	const auto& stats = first.Stats;
	SWIM_CHECK_EQUAL(stats.Tested, width * height);
	SWIM_CHECK_EQUAL(stats.FrustumCulled + stats.NotDrawable + stats.Visible, stats.Tested);
	SWIM_CHECK(stats.Visible > 400u && stats.FrustumCulled > 90000u && stats.NotDrawable > 0u);
	std::uint32_t lodTotal = 0;
	for (const auto count : stats.LodCounts)
	{
		lodTotal += count;
	}
	SWIM_CHECK_EQUAL(lodTotal, stats.Visible);
	SWIM_CHECK_EQUAL(stats.Draws + stats.Dropped, stats.Visible); // One submesh per LOD, one index page.
	SWIM_CHECK(stats.Dropped > 0u);
	SWIM_CHECK_EQUAL(first.Bins[1].size(), 16u);
	SWIM_CHECK_EQUAL(Total(first), stats.Draws);
	SWIM_CHECK(std::memcmp(&first.Stats, &second.Stats, sizeof(VisibilityStats)) == 0);
	SWIM_CHECK_EQUAL(Total(second), Total(first));
	std::printf("             [Visibility benchmark] 100k rows: first frame %.2f ms, steady frame %.2f ms (CPU reference), %u visible\n",
		std::chrono::duration<double, std::milli>(middle - start).count(), std::chrono::duration<double, std::milli>(end - middle).count(),
		stats.Visible);
}
