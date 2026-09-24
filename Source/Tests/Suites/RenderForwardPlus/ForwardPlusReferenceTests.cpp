#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusReference.h"
#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace Fp = Swim::Render::ForwardPlus;
namespace Pbr = Swim::Render::StandardPbr;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	float Dot(const Fp::Float3& a, const Fp::Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	float Length(const Fp::Float3& v)
	{
		return std::sqrt(Dot(v, v));
	}

	// A random rotation times a non-uniform scale, optionally mirrored, plus a translation.
	GpuTransformRecord RandomTransform(std::mt19937& random, bool mirrored)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const float a = unit(random) * 6.28f;
		const float b = unit(random) * 6.28f;
		// Rz(a) * Rx(b).
		const float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
		const float r[9]{ ca, -sa * cb, sa * sb, sa, ca * cb, -ca * sb, 0, sb, cb };
		const float scale[3]{ (mirrored ? -1.0f : 1.0f) * (0.5f + 2.0f * unit(random)), 0.5f + 2.0f * unit(random),
			0.5f + 2.0f * unit(random) };
		GpuTransformRecord transform;
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				transform.Current[row * 4 + column] = r[row * 3 + column] * scale[column];
			}
			transform.Current[row * 4 + 3] = 10.0f * unit(random) - 5.0f;
		}
		return transform;
	}

	Pbr::ResolvedSurface RandomSurface(std::mt19937& random)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		Pbr::ResolvedSurface surface;
		surface.Normal = Pbr::Normalize({ unit(random) - 0.5f, 1.0f, unit(random) - 0.5f });
		surface.BaseColor = { unit(random), unit(random), unit(random) };
		surface.Metallic = unit(random);
		surface.PerceptualRoughness = 0.1f + 0.9f * unit(random);
		surface.Occlusion = 0.5f + 0.5f * unit(random);
		surface.Emissive = { 0.1f * unit(random), 0.2f * unit(random), 0.0f };
		surface.Alpha = unit(random);
		return surface;
	}

	ForwardViewRecord ViewRecord(const ClusterView& camera, bool environment)
	{
		ForwardPlusView view;
		view.ViewProjection = camera.Projection;
		// Camera position/forward from the view rows (row-major world->view).
		const auto& v = camera.View;
		for (int c = 0; c < 3; ++c)
		{
			view.CameraPosition[c] = -(v[0 * 4 + c] * v[3] + v[1 * 4 + c] * v[7] + v[2 * 4 + c] * v[11]);
			view.CameraForward[c] = -v[2 * 4 + c];
		}
		view.Ambient = { 0.03f, 0.04f, 0.05f };
		view.EnvironmentIntensity = 0.7f;
		view.EnvironmentRotation = 0.4f;
		return BuildForwardViewRecord(view, 4, 3, environment);
	}

	Environment::EnvironmentProbe UniformProbe()
	{
		Environment::IrradianceSh sh;
		sh.Coefficients[0] = { 0.6f, 0.7f, 0.8f };
		sh.Coefficients[1] = { 0.1f, 0.0f, -0.1f };
		Environment::CubeImage cube(4, 3);
		for (std::uint32_t mip = 0; mip < 3; ++mip)
		{
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				for (auto& texel : cube.Face(mip, face))
				{
					texel = { 0.3f + 0.1f * float(face), 0.5f, 0.2f * float(mip), 1.0f };
				}
			}
		}
		return { sh, cube, Environment::BuildBrdfLut(8, 16) };
	}
} // namespace

SWIM_TEST("Render.ForwardPlus.Reference", "MaterialsRouteToTheOpaqueOrTransparentBin")
{
	Pbr::Parameters parameters;
	SWIM_CHECK(Fp::MaterialBin(parameters) == ForwardPlusBin::Opaque);
	parameters.Flags = Pbr::FlagAlphaMask | Pbr::FlagDoubleSided;
	SWIM_CHECK(Fp::MaterialBin(parameters) == ForwardPlusBin::Opaque); // Masked and double-sided stay opaque.
	parameters.Flags = Pbr::FlagAlphaBlend;
	SWIM_CHECK(Fp::MaterialBin(parameters) == ForwardPlusBin::Transparent);
	parameters.Flags = Pbr::FlagAlphaBlend | Pbr::FlagDoubleSided;
	SWIM_CHECK(Fp::MaterialBin(parameters) == ForwardPlusBin::Transparent);
	SWIM_CHECK_EQUAL(ForwardPlusBinCount, 2u);

	// Back faces: culled for single-sided materials only.
	parameters.Flags = 0;
	SWIM_CHECK(Fp::CullsFace(parameters, false));
	SWIM_CHECK(!Fp::CullsFace(parameters, true));
	parameters.Flags = Pbr::FlagDoubleSided;
	SWIM_CHECK(!Fp::CullsFace(parameters, false));
}

SWIM_TEST("Render.ForwardPlus.Reference", "NormalsStayPerpendicularAndOutwardUnderScaleAndMirroring")
{
	std::mt19937 random(66);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	for (int trial = 0; trial < 400; ++trial)
	{
		const bool mirrored = trial % 2 == 1;
		const auto transform = RandomTransform(random, mirrored);
		SWIM_CHECK((Fp::Determinant(transform.Current) < 0.0f) == mirrored);
		// A surface point p with normal n, and two tangent directions.
		const auto n = Pbr::Normalize({ unit(random), unit(random), unit(random) });
		const auto t0 = Pbr::Normalize({ n[1] - n[2], n[2] - n[0], n[0] - n[1] });
		const Fp::Float3 t1{ n[1] * t0[2] - n[2] * t0[1], n[2] * t0[0] - n[0] * t0[2], n[0] * t0[1] - n[1] * t0[0] };
		const auto normal = Fp::TransformNormal(transform.Current, n);
		for (const auto& t : { t0, t1 })
		{
			const auto direction = Fp::TransformDirection(transform.Current, t);
			SWIM_CHECK(std::abs(Dot(normal, direction)) < 1.0e-4f * Length(normal) * Length(direction));
		}
		// Outward stays outward: the transformed offset p + n - p has a positive component along the normal.
		const Fp::Float3 p{ unit(random), unit(random), unit(random) };
		const auto a = Fp::TransformPoint(transform.Current, p);
		const auto b = Fp::TransformPoint(transform.Current, { p[0] + n[0], p[1] + n[1], p[2] + n[2] });
		SWIM_CHECK(Dot(normal, { b[0] - a[0], b[1] - a[1], b[2] - a[2] }) > 0.0f);
	}
	// The identity is exact.
	GpuTransformRecord identity;
	const auto same = Fp::TransformNormal(identity.Current, { 0.0f, 0.6f, 0.8f });
	SWIM_CHECK(same == (Fp::Float3{ 0.0f, 0.6f, 0.8f }));
}

SWIM_TEST("Render.ForwardPlus.Reference", "ShadingFramesFollowTheTangentSignAndMirroring")
{
	const auto frame = Fp::BuildFrame({ 0, 0, 2 }, { 3, 0, 0.5f, 1 }, true, false);
	SWIM_CHECK(frame.Normal == (Fp::Float3{ 0, 0, 1 }));
	SWIM_CHECK(std::abs(frame.Tangent[0] - 1.0f) < 1.0e-6f && std::abs(frame.Tangent[2]) < 1.0e-6f); // Orthogonalized.
	SWIM_CHECK(std::abs(frame.Bitangent[1] - 1.0f) < 1.0e-6f);
	SWIM_CHECK(frame.FrontFacing);
	const auto flipped = Fp::BuildFrame({ 0, 0, 1 }, { 1, 0, 0, -1 }, true, false);
	SWIM_CHECK(std::abs(flipped.Bitangent[1] + 1.0f) < 1.0e-6f);
	// A mirroring transform reverses the winding: the rasterizer's back face is the front.
	SWIM_CHECK(Fp::BuildFrame({ 0, 0, 1 }, { 1, 0, 0, 1 }, false, true).FrontFacing);
	SWIM_CHECK(!Fp::BuildFrame({ 0, 0, 1 }, { 1, 0, 0, 1 }, true, true).FrontFacing);
}

SWIM_TEST("Render.ForwardPlus.Reference", "TransparentDrawsSortBackToFrontIndependentOfCompactionOrder")
{
	std::mt19937 random(67);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	const auto camera = Scene::Camera(16.0f / 9.0f);
	const auto view = ViewRecord(camera, false);
	constexpr std::uint32_t objects = 64;
	std::vector<GpuInstanceRecord> instances(objects);
	std::vector<GpuTransformRecord> transforms(objects);
	for (std::uint32_t i = 0; i < objects; ++i)
	{
		// Pairs of objects share a position (equal depths) to exercise the tie-breaks.
		instances[i].TransformIndex = objects - 1 - i;
		auto& t = transforms[objects - 1 - i];
		t.Current[3] = i % 2 == 1 ? transforms[objects - i].Current[3] : 4.0f * unit(random) - 2.0f;
		t.Current[7] = 1.0f;
		t.Current[11] = -float(i / 2) * 0.75f;
	}
	// Every object draws two submeshes.
	std::vector<GpuDrawRecord> records;
	for (std::uint32_t i = 0; i < objects; ++i)
	{
		records.push_back({ i, 2 * i + 1 });
		records.push_back({ i, 2 * i });
	}
	const auto sequence = [&](const std::vector<GpuDrawRecord>& bin, std::uint32_t first)
	{
		std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
		for (const auto slot : Fp::SortTransparentDraws(bin, first, instances, transforms, view))
		{
			result.emplace_back(bin[slot - first].InstanceRow, bin[slot - first].SubmeshRow);
		}
		return result;
	};
	const auto reference = sequence(records, 100);
	SWIM_REQUIRE_EQUAL(reference.size(), records.size());
	for (std::size_t i = 1; i < reference.size(); ++i)
	{
		const auto& a = instances[reference[i - 1].first];
		const auto& b = instances[reference[i].first];
		const float da = Fp::SortDepth(a, transforms[a.TransformIndex], view);
		const float db = Fp::SortDepth(b, transforms[b.TransformIndex], view);
		SWIM_CHECK(da >= db); // Farther first.
		if (da == db)
		{
			SWIM_CHECK(reference[i - 1] < reference[i]); // Instance row, then submesh row.
		}
	}
	// The GPU compaction order is arbitrary; the draw order is not.
	for (int shuffle = 0; shuffle < 20; ++shuffle)
	{
		auto shuffled = records;
		std::shuffle(shuffled.begin(), shuffled.end(), random);
		SWIM_CHECK(sequence(shuffled, 7) == reference);
	}
	// Depth is the bounds center along the camera's forward axis.
	GpuInstanceRecord probe;
	probe.LocalCenter[2] = -1.0f;
	GpuTransformRecord moved;
	moved.Current[11] = -5.0f;
	auto straight = view;
	straight.CameraPosition[0] = straight.CameraPosition[1] = straight.CameraPosition[2] = 0.0f;
	straight.CameraForward[0] = straight.CameraForward[1] = 0.0f;
	straight.CameraForward[2] = -1.0f;
	SWIM_CHECK(std::abs(Fp::SortDepth(probe, moved, straight) - 6.0f) < 1.0e-6f);
	ForwardSortEntry near{ 1.0f, 0, 0, 0 };
	ForwardSortEntry far{ 2.0f, 5, 0, 1 };
	SWIM_CHECK(Fp::SortsBefore(far, near) && !Fp::SortsBefore(near, far) && !Fp::SortsBefore(near, near));
}

SWIM_TEST("Render.ForwardPlus.Reference", "ShadingSumsClusteredLightsAmbientEnvironmentAndEmission")
{
	const auto scene = Scene::RandomScene(2, 300, 166);
	ClusterGridDesc gridDesc;
	gridDesc.ViewportWidth = 640;
	gridDesc.ViewportHeight = 360;
	gridDesc.TileSize = 32;
	gridDesc.SliceCount = 16;
	gridDesc.Far = 60.0f;
	gridDesc.MaxLightsPerCluster = 256;
	gridDesc.IndexCapacity = 1u << 20;
	const auto camera = Scene::Camera(16.0f / 9.0f);
	const auto grid = MakeClusterGridRecord(gridDesc, camera);
	const auto assignment = Clustering::AssignLights(grid, scene.Rows, scene.Header);
	SWIM_REQUIRE_EQUAL(assignment.Stats.OverflowClusters, 0u);
	const auto probe = UniformProbe();
	const auto lit = ViewRecord(camera, true);
	const auto unlit = ViewRecord(camera, false);

	Fp::LightingInputs clustered{ scene.Rows, scene.Header, &grid, assignment.Records, assignment.Indices, &probe };
	Fp::LightingInputs brute{ scene.Rows, scene.Header, nullptr, {}, {}, &probe };
	std::mt19937 random(266);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::uint32_t litByLocal = 0;
	for (int i = 0; i < 500; ++i)
	{
		const float px = unit(random) * 640.0f;
		const float py = unit(random) * 360.0f;
		const float depth = 0.5f + 40.0f * unit(random);
		const auto position = Scene::ViewToWorld(grid, Scene::ViewPoint(grid, px, py, depth));
		SWIM_CHECK(std::abs(Fp::ViewDepth(grid, position) - depth) < 1.0e-3f * depth);
		const auto surface = RandomSurface(random);
		const auto a = Fp::Shade(clustered, lit, surface, position, px, py);
		const auto b = Fp::Shade(brute, lit, surface, position, px, py);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(a[c] - b[c]) <= 1.0e-5f + 1.0e-4f * std::abs(b[c])); // Clustered == brute force.
		}
		SWIM_CHECK(a[3] == surface.Alpha);

		// Decomposition: lights + ambient + IBL + emission.
		const auto toCamera = Pbr::Normalize(
			{ lit.CameraPosition[0] - position[0], lit.CameraPosition[1] - position[1], lit.CameraPosition[2] - position[2] });
		const auto direct = Lights::ShadeAllLights(scene.Rows, scene.Header,
			{ surface.BaseColor, surface.Metallic, surface.PerceptualRoughness }, surface.Normal, toCamera, position);
		const auto ibl = Pbr::EvaluateEnvironment(surface, toCamera, probe.Lookup(surface, toCamera, { 0.7f, 0.4f }));
		const auto withoutIbl = Fp::Shade(brute, unlit, surface, position, px, py);
		for (int c = 0; c < 3; ++c)
		{
			const float expected = direct[c] + lit.Ambient[c] * surface.BaseColor[c] * surface.Occlusion + surface.Emissive[c];
			SWIM_CHECK(std::abs(withoutIbl[c] - expected) <= 1.0e-5f + 1.0e-5f * expected);
			SWIM_CHECK(std::abs(b[c] - (expected + ibl[c])) <= 1.0e-5f + 1.0e-5f * (expected + ibl[c]));
		}
		// Item 76: the indirect part is exactly ambient + IBL, and the rest is direct + emission.
		const auto indirect = Fp::IndirectRadiance(brute, lit, surface, position);
		const auto indirectUnlit = Fp::IndirectRadiance(brute, unlit, surface, position);
		for (int c = 0; c < 3; ++c)
		{
			const float ambient = lit.Ambient[c] * surface.BaseColor[c] * surface.Occlusion;
			SWIM_CHECK(std::abs(indirect[c] - (ambient + ibl[c])) <= 1.0e-6f + 1.0e-5f * (ambient + ibl[c]));
			SWIM_CHECK(std::abs(indirectUnlit[c] - ambient) <= 1.0e-7f + 1.0e-6f * ambient); // No environment: ambient only.
			const float rest = direct[c] + surface.Emissive[c];
			SWIM_CHECK(std::abs(b[c] - indirect[c] - rest) <= 1.0e-5f + 1.0e-4f * rest);
		}
		const auto directionalOnly = Lights::ShadeAllLights(scene.Rows, { 2, 0, scene.Header.FirstLocalRow, 0 },
			{ surface.BaseColor, surface.Metallic, surface.PerceptualRoughness }, surface.Normal, toCamera, position);
		litByLocal += direct[0] > directionalOnly[0] + 1.0e-4f ? 1u : 0u;
	}
	SWIM_CHECK(litByLocal > 25u); // Local lights matter in this scene.

	// A truncated list only removes light.
	auto tightDesc = gridDesc;
	tightDesc.MaxLightsPerCluster = 2;
	auto tightGrid = MakeClusterGridRecord(tightDesc, camera);
	const auto truncated = Clustering::AssignLights(tightGrid, scene.Rows, scene.Header);
	SWIM_REQUIRE(truncated.Stats.OverflowClusters > 0u);
	Fp::LightingInputs partial{ scene.Rows, scene.Header, &tightGrid, truncated.Records, truncated.Indices, nullptr };
	for (int i = 0; i < 200; ++i)
	{
		const float px = unit(random) * 640.0f;
		const float py = unit(random) * 360.0f;
		const auto position = Scene::ViewToWorld(tightGrid, Scene::ViewPoint(tightGrid, px, py, 1.0f + 30.0f * unit(random)));
		const auto surface = RandomSurface(random);
		const auto some = Fp::Shade(partial, unlit, surface, position, px, py);
		const auto all = Fp::Shade(brute, unlit, surface, position, px, py);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(some[c] <= all[c] * (1.0f + 1.0e-5f) + 1.0e-6f);
		}
	}
}

SWIM_TEST("Render.ForwardPlus.Reference", "DebugHeatmapAndPremultipliedBlending")
{
	const auto scene = Scene::RandomScene(0, 200, 167);
	ClusterGridDesc gridDesc;
	gridDesc.ViewportWidth = 320;
	gridDesc.ViewportHeight = 180;
	gridDesc.TileSize = 32;
	gridDesc.SliceCount = 8;
	gridDesc.Far = 50.0f;
	gridDesc.MaxLightsPerCluster = 6;
	gridDesc.IndexCapacity = 1u << 16;
	const auto grid = MakeClusterGridRecord(gridDesc, Scene::Camera(16.0f / 9.0f));
	const auto assignment = Clustering::AssignLights(grid, scene.Rows, scene.Header);
	std::uint32_t black = 0, magenta = 0, ramp = 0;
	for (std::uint32_t y = 0; y < 180; y += 7)
	{
		for (std::uint32_t x = 0; x < 320; x += 7)
		{
			for (const float depth : { 0.5f, 5.0f, 20.0f, 45.0f })
			{
				const auto color = Fp::DebugColor(grid, assignment.Records, float(x) + 0.5f, float(y) + 0.5f, depth);
				const auto& record = assignment.Records[ClusterIndexFor(grid, float(x) + 0.5f, float(y) + 0.5f, depth)];
				const auto heat = Clustering::HeatmapColor(record.Count, record.RawCount, gridDesc.MaxLightsPerCluster);
				SWIM_CHECK(color[3] == 1.0f);
				if (record.RawCount == 0)
				{
					SWIM_CHECK((color == Fp::Float4{ 0, 0, 0, 1 }));
					++black;
				}
				else
				{
					SWIM_CHECK(color == heat);
					magenta += record.Count < record.RawCount ? 1u : 0u;
					ramp += record.Count == record.RawCount ? 1u : 0u;
				}
			}
		}
	}
	SWIM_CHECK(black > 0u && magenta > 0u && ramp > 0u);

	// Over: alpha 0 keeps the destination, alpha 1 replaces it, and order matters.
	const Fp::Float4 background{ 0.2f, 0.4f, 0.6f, 1.0f };
	const Fp::Float4 red{ 1.0f, 0.0f, 0.0f, 0.5f };
	const Fp::Float4 green{ 0.0f, 1.0f, 0.0f, 0.25f };
	SWIM_CHECK(Fp::Over({ 5, 5, 5, 0 }, background) == background);
	SWIM_CHECK((Fp::Over({ 0.1f, 0.2f, 0.3f, 1.0f }, background) == Fp::Float4{ 0.1f, 0.2f, 0.3f, 1.0f }));
	const auto redThenGreen = Fp::Over(green, Fp::Over(red, background));
	const auto greenThenRed = Fp::Over(red, Fp::Over(green, background));
	SWIM_CHECK(std::abs(redThenGreen[0] - greenThenRed[0]) > 0.1f);
	SWIM_CHECK(std::abs(redThenGreen[0] - (0.25f * 0.0f + 0.75f * (0.5f * 1.0f + 0.5f * 0.2f))) < 1.0e-6f);
	SWIM_CHECK(std::abs(redThenGreen[3] - 1.0f) < 1.0e-6f);
}

SWIM_TEST("Render.ForwardPlus.Reference", "ViewRecordsNormalizeAndValidate")
{
	ForwardPlusView view;
	view.CameraPosition = { 1, 2, 3 };
	view.CameraForward = { 0, 0, -4 };
	view.Ambient = { 0.1f, 0.2f, 0.3f };
	view.EnvironmentIntensity = 0.5f;
	view.EnvironmentRotation = 1.25f;
	view.DebugMode = ForwardPlusDebugMode::ClusterHeatmap;
	const auto record = BuildForwardViewRecord(view, 12, 6, true);
	SWIM_CHECK(record.CameraForward[2] == -1.0f);
	SWIM_CHECK(record.CameraPosition[1] == 2.0f);
	SWIM_CHECK(record.Ambient[2] == 0.3f);
	SWIM_CHECK_EQUAL(record.MaterialCount, 12u);
	SWIM_CHECK_EQUAL(record.PrefilteredMipCount, 6u);
	SWIM_CHECK_EQUAL(record.Flags, ForwardViewFlagEnvironment);
	SWIM_CHECK_EQUAL(record.DebugMode, 1u);
	SWIM_CHECK(record.EnvironmentIntensity == 0.5f && record.EnvironmentRotation == 1.25f);
	const auto plain = BuildForwardViewRecord(ForwardPlusView{}, 0, 0, false);
	SWIM_CHECK_EQUAL(plain.Flags, 0u);
	SWIM_CHECK_EQUAL(plain.PrefilteredMipCount, 1u);

	auto bad = view;
	bad.CameraForward = { 0, 0, 0 };
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
	bad = view;
	bad.ViewProjection[5] = NAN;
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
	bad = view;
	bad.Ambient[0] = -0.1f;
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
	bad = view;
	bad.EnvironmentIntensity = -1.0f;
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
	bad = view;
	bad.DebugMode = static_cast<ForwardPlusDebugMode>(9);
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
}

SWIM_TEST("Render.ForwardPlus.Reference", "MotionVectorsFollowObjectAndCameraMotion")
{
	// The previous matrix defaults to this frame's; jitter is copied, never applied to it.
	ForwardPlusView view;
	view.Jitter = { 0.25f, -0.5f };
	const auto still = BuildForwardViewRecord(view, 1, 1, false);
	SWIM_CHECK(std::equal(std::begin(still.PreviousViewProjection), std::end(still.PreviousViewProjection), view.ViewProjection.begin()));
	SWIM_CHECK(still.Jitter[0] == 0.25f && still.Jitter[1] == -0.5f);
	SWIM_CHECK(still.Reserved1[0] == 0.0f && still.Reserved1[1] == 0.0f);

	const float identity[12]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
	float movedRight[12]{ 1, 0, 0, 0.2f, 0, 1, 0, 0, 0, 0, 1, 0 };
	float movedUp[12]{ 1, 0, 0, 0, 0, 1, 0, 0.2f, 0, 0, 1, 0 };
	const Fp::Float3 local{ 0.1f, -0.3f, 0.5f };
	const auto none = Fp::MotionVector(still, identity, identity, local);
	SWIM_CHECK(none[0] == 0.0f && none[1] == 0.0f); // Jitter never shows up as motion.
	// NDC spans 2 per UV unit, and UV y points down.
	const auto right = Fp::MotionVector(still, movedRight, identity, local);
	SWIM_CHECK(std::abs(right[0] - 0.1f) < 1e-6f && std::abs(right[1]) < 1e-6f);
	const auto up = Fp::MotionVector(still, movedUp, identity, local);
	SWIM_CHECK(std::abs(up[0]) < 1e-6f && std::abs(up[1] + 0.1f) < 1e-6f);

	// Camera motion alone: the previous camera saw the point 0.4 NDC further left.
	auto panned = view;
	panned.PreviousViewProjection = std::array<float, 16>{ 1, 0, 0, -0.4f, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const auto pan = Fp::MotionVector(BuildForwardViewRecord(panned, 1, 1, false), identity, identity, local);
	SWIM_CHECK(std::abs(pan[0] - 0.2f) < 1e-6f && std::abs(pan[1]) < 1e-6f);

	// Perspective: a point moving toward the camera slides away from the centre.
	ForwardPlusView deep;
	deep.ViewProjection = PerspectiveReverseZRowMajor(1.2f, 1.0f, 0.1f);
	const auto deepRecord = BuildForwardViewRecord(deep, 1, 1, false);
	const float near[12]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2 };
	const auto approach = Fp::MotionVector(deepRecord, near, identity, { 0.5f, 0.5f, -5.0f });
	SWIM_CHECK(approach[0] > 0.0f && approach[1] < 0.0f);
	SWIM_CHECK(std::abs(approach[0] + approach[1]) < 1e-6f); // Symmetric in x and y (square aspect).

	auto bad = view;
	bad.Jitter[1] = INFINITY;
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
	bad = panned;
	(*bad.PreviousViewProjection)[3] = NAN;
	SWIM_CHECK_THROWS(BuildForwardViewRecord(bad, 1, 1, false), std::invalid_argument);
}

SWIM_TEST("Render.ForwardPlus.Reference", "ShadowedLightsAreScaledByTheirShadowFactor")
{
	namespace Sh = Swim::Render::Shadows;
	// Two suns; the first casts through shadow record 0, whose one cascade looks
	// straight down on the ground. The left half of its 16x16 atlas holds an occluder.
	LightDesc desc;
	desc.Type = LightType::Directional;
	desc.Direction = { 0.2f, -1.0f, 0.1f };
	desc.Intensity = 3.0f;
	desc.ShadowIndex = 0;
	desc.Flags = LightFlags::CastsShadows;
	const auto shadowed = Lights::EncodeLight(desc);
	desc.Direction = { -0.3f, -1.0f, 0.0f };
	desc.Color = { 0.2f, 0.5f, 1.0f };
	desc.ShadowIndex = GpuLightNoShadow;
	desc.Flags = LightFlags::None;
	const auto open = Lights::EncodeLight(desc);
	auto unflagged = shadowed;
	unflagged.Flags = 0; // A ShadowIndex without CastsShadows is ignored.

	GpuShadowRecord record;
	record.Kind = static_cast<std::uint32_t>(ShadowKind::Directional);
	record.ViewCount = 1;
	record.CascadeFar[0] = 1.0e9f;
	GpuShadowView shadowView;
	const auto viewProjection =
		MultiplyRowMajor(OrthographicReverseZRowMajor(-10, 10, -10, 10, 0.0f, 20.0f), Sh::LookAlong({ 0, 10, 0 }, { 0, -1, 0 }));
	std::copy(viewProjection.begin(), viewProjection.end(), shadowView.ViewProjection);
	shadowView.AtlasRect[2] = shadowView.AtlasRect[3] = 16.0f;
	shadowView.TexelWorldSize = 20.0f / 16.0f;
	Sh::ShadowAtlasImage atlas;
	atlas.Size = 16;
	atlas.Depth.assign(256, 0.0f);
	for (std::uint32_t y = 0; y < 16; ++y)
	{
		for (std::uint32_t x = 0; x < 8; ++x)
		{
			atlas.Depth[y * 16 + x] = 0.9f; // The ground is at depth 0.5.
		}
	}
	const std::vector<GpuShadowRecord> records{ record };
	const std::vector<GpuShadowView> views{ shadowView };
	const Sh::ShadowSampleInputs shadowInputs{ &atlas, records, views };

	const auto camera = Scene::Camera(16.0f / 9.0f);
	auto withShadows = ViewRecord(camera, false);
	withShadows.Flags |= ForwardViewFlagShadows;
	const auto withoutShadows = ViewRecord(camera, false);
	const std::vector<GpuLightRecord> both{ shadowed, open };
	const std::vector<GpuLightRecord> openOnly{ open };
	const std::vector<GpuLightRecord> ignored{ unflagged, open };
	const GpuLightHeader twoSuns{ 2, 0, 2, 0 };
	const GpuLightHeader oneSun{ 1, 0, 1, 0 };
	const Fp::LightingInputs inputs{ both, twoSuns, nullptr, {}, {}, nullptr, &shadowInputs };
	const Fp::LightingInputs openInputs{ openOnly, oneSun, nullptr, {}, {}, nullptr, nullptr };
	const Fp::LightingInputs ignoredInputs{ ignored, twoSuns, nullptr, {}, {}, nullptr, &shadowInputs };
	const Fp::LightingInputs noAtlas{ both, twoSuns, nullptr, {}, {}, nullptr, nullptr };

	std::mt19937 random(72);
	std::uniform_real_distribution<float> unit(-9.0f, 9.0f);
	std::uint32_t inShadow = 0, inLight = 0;
	for (int i = 0; i < 400; ++i)
	{
		auto surface = RandomSurface(random);
		surface.Normal = { 0, 1, 0 };
		const Fp::Float3 position{ unit(random), 0.0f, unit(random) };
		const auto projected = Sh::ProjectToShadowView(shadowView, position);
		SWIM_REQUIRE(projected.has_value());
		const float px = projected->PixelX;
		const bool dark = px >= 2.0f && px < 6.0f;
		const bool bright = px >= 10.0f && px < 14.0f;
		if (!dark && !bright)
		{
			continue;
		}
		const auto result = Fp::Shade(inputs, withShadows, surface, position, 0, 0);
		const auto expected = dark ? Fp::Shade(openInputs, withoutShadows, surface, position, 0, 0)
								   : Fp::Shade(inputs, withoutShadows, surface, position, 0, 0);
		const auto unshadowedFlag = Fp::Shade(ignoredInputs, withShadows, surface, position, 0, 0);
		const auto unshadowedView = Fp::Shade(inputs, withoutShadows, surface, position, 0, 0);
		const auto unshadowedAtlas = Fp::Shade(noAtlas, withShadows, surface, position, 0, 0);
		for (int c = 0; c < 4; ++c)
		{
			SWIM_CHECK(std::abs(result[c] - expected[c]) <= 1.0e-6f + 1.0e-5f * std::abs(expected[c]));
			SWIM_CHECK(unshadowedFlag[c] == unshadowedView[c] && unshadowedAtlas[c] == unshadowedView[c]);
		}
		SWIM_CHECK_EQUAL(Fp::LightShadow(inputs, withShadows, shadowed, position, surface.Normal, { 0, 1, 0 }), dark ? 0.0f : 1.0f);
		SWIM_CHECK_EQUAL(Fp::LightShadow(inputs, withShadows, open, position, surface.Normal, { 0, 1, 0 }), 1.0f);
		(dark ? inShadow : inLight) += 1;
	}
	SWIM_CHECK(inShadow > 50u && inLight > 50u);

	// The packed view carries the flag.
	ForwardPlusView packed;
	SWIM_CHECK((BuildForwardViewRecord(packed, 1, 1, false, true).Flags & ForwardViewFlagShadows) != 0u);
	SWIM_CHECK((BuildForwardViewRecord(packed, 1, 1, true, false).Flags & ForwardViewFlagShadows) == 0u);
}
