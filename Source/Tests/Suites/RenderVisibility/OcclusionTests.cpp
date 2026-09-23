#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"
#include "Engine/Systems/Renderer/Visibility/HzbPyramid.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityMath.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	constexpr auto Drawable = RenderObjectFlags::Live | RenderObjectFlags::HasMesh | RenderObjectFlags::Visible;

	std::array<float, 4> Clip(const std::array<float, 16>& m, float x, float y, float z)
	{
		return { m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7], m[8] * x + m[9] * y + m[10] * z + m[11],
			m[12] * x + m[13] * y + m[14] * z + m[15] };
	}

	// Camera at (cx, cy, 10) looking down -Z through a 20x20 window, reverse-Z by default.
	RenderViewDesc OrthoDesc(float cx = 0.0f, float cy = 0.0f, DepthConvention depth = DepthConvention::ReverseZ, std::uint32_t flags = 0)
	{
		RenderViewDesc desc;
		const std::array<float, 16> view{ 1, 0, 0, -cx, 0, 1, 0, -cy, 0, 0, 1, -10, 0, 0, 0, 1 };
		const auto projection = depth == DepthConvention::ReverseZ ? OrthographicReverseZRowMajor(-10, 10, -10, 10, 0.1f, 100.0f)
																   : OrthographicRowMajor(-10, 10, -10, 10, 0.1f, 100.0f);
		desc.ViewProjection = MultiplyRowMajor(projection, view);
		desc.CameraPosition = { cx, cy, 10 };
		desc.LodScale = 1.0f;
		desc.Depth = depth;
		desc.Flags = flags;
		return desc;
	}

	// A CPU depth buffer that rasterizes axis-aligned world quads (constant z) with the
	// same +Y-up pixel mapping as the RHI, keeping the nearer depth per pixel center.
	struct DepthImage
	{
		DepthImage(std::uint32_t width, std::uint32_t height, const RenderViewDesc& view)
			: Width(width), Height(height), View(view), Depth(std::size_t(width) * height, DepthClearValue(view.Depth))
		{
		}

		void Quad(float cx, float cy, float ex, float ey, float z)
		{
			// Conservative pixel bounds of the rectangle; the exact test is per pixel center.
			const auto column = [&](float wx)
			{
				return ((wx - View.CameraPosition[0]) / 10.0f + 1.0f) * 0.5f * float(Width);
			};
			const auto row = [&](float wy)
			{
				return (1.0f - (wy - View.CameraPosition[1]) / 10.0f) * 0.5f * float(Height);
			};
			const auto clampTo = [](float value, std::uint32_t limit)
			{
				return std::uint32_t(std::clamp(value, 0.0f, float(limit)));
			};
			const auto x0 = clampTo(std::floor(column(cx - ex)) - 1.0f, Width);
			const auto x1 = clampTo(std::ceil(column(cx + ex)) + 1.0f, Width);
			const auto y0 = clampTo(std::floor(row(cy + ey)) - 1.0f, Height);
			const auto y1 = clampTo(std::ceil(row(cy - ey)) + 1.0f, Height);
			for (std::uint32_t y = y0; y < y1; ++y)
			{
				for (std::uint32_t x = x0; x < x1; ++x)
				{
					// Invert the orthographic xy mapping at the pixel center.
					const float ndcX = (float(x) + 0.5f) / float(Width) * 2.0f - 1.0f;
					const float ndcY = 1.0f - (float(y) + 0.5f) / float(Height) * 2.0f;
					const float wx = View.CameraPosition[0] + ndcX * 10.0f;
					const float wy = View.CameraPosition[1] + ndcY * 10.0f;
					if (wx < cx - ex || wx > cx + ex || wy < cy - ey || wy > cy + ey)
					{
						continue;
					}
					const auto clip = Clip(View.ViewProjection, wx, wy, z);
					auto& texel = Depth[std::size_t(y) * Width + x];
					texel = NearerDepth(View.Depth, texel, clip[2] / clip[3]);
				}
			}
		}

		HzbReference Hzb() const { return HzbReference::Build(Depth, Width, Height, View.Depth); }

		std::uint32_t Width;
		std::uint32_t Height;
		RenderViewDesc View;
		std::vector<float> Depth;
	};

	// Rows of flat quads (extents ex, ey at depth z) sharing one single-LOD mesh.
	struct QuadScene
	{
		QuadScene()
		{
			GpuMeshMetadata mesh;
			mesh.IndexPage = 0;
			mesh.FirstSubmesh = 0;
			mesh.SubmeshCount = 1;
			mesh.LodCount = 1;
			mesh.Lods[0] = { 0, 1, 0.0f, 0 };
			Meshes.push_back(mesh);
			Submeshes.push_back({ 0, 6, 0, 0 });
		}

		std::uint32_t Add(float x, float y, float z, float ex, float ey)
		{
			GpuInstanceRecord instance;
			const auto row = static_cast<std::uint32_t>(Instances.size());
			instance.MeshIndex = 0;
			instance.TransformIndex = row;
			instance.MaterialSet = 0;
			instance.ObjectId = row;
			instance.Flags = static_cast<std::uint32_t>(Drawable);
			instance.Generation = 1;
			instance.LocalExtents[0] = ex;
			instance.LocalExtents[1] = ey;
			Instances.push_back(instance);
			GpuTransformRecord transform;
			Transforms.push_back(transform);
			Move(row, x, y, z);
			return row;
		}

		void Move(std::uint32_t row, float x, float y, float z)
		{
			Transforms[row].Current[3] = x;
			Transforms[row].Current[7] = y;
			Transforms[row].Current[11] = z;
		}

		VisibilityReferenceResult Run(const RenderViewDesc& view, VisibilityPhase phase, const HzbReference* hzb = nullptr)
		{
			const VisibilityBinLayout bins(std::vector<std::uint32_t>{ Capacity }, 1);
			const std::vector<std::uint32_t> materialBins{ 0 };
			const std::vector<std::uint32_t> pages{ 0 };
			VisibilityReferenceInputs inputs{ Instances, Transforms, Meshes, Submeshes, BuildGpuViewRecord(view), materialBins, pages,
				&bins };
			inputs.Phase = phase;
			inputs.Hzb = hzb;
			return RunVisibilityReference(inputs, Lods, History);
		}

		// "Draws" every emitted row into the depth image, as the draw pass would.
		void Draw(const VisibilityReferenceResult& result, DepthImage& depth) const
		{
			for (const auto& bin : result.Bins)
			{
				for (const auto& draw : bin)
				{
					const auto row = draw.Record.InstanceRow;
					const auto& t = Transforms[row].Current;
					depth.Quad(t[3], t[7], Instances[row].LocalExtents[0], Instances[row].LocalExtents[1], t[11]);
				}
			}
		}

		std::vector<std::uint32_t> DrawnRows(const VisibilityReferenceResult& result) const
		{
			std::vector<std::uint32_t> rows;
			for (const auto& bin : result.Bins)
			{
				for (const auto& draw : bin)
				{
					rows.push_back(draw.Record.InstanceRow);
				}
			}
			std::sort(rows.begin(), rows.end());
			return rows;
		}

		std::vector<GpuInstanceRecord> Instances;
		std::vector<GpuTransformRecord> Transforms;
		std::vector<GpuMeshMetadata> Meshes;
		std::vector<GpuSubmeshRecord> Submeshes;
		std::vector<GpuLodState> Lods;
		std::vector<std::uint32_t> History;
		std::uint32_t Capacity = 256;
	};

	// One two-phase frame on the CPU: early draw, HZB of that depth, late draw.
	struct PhaseFrame
	{
		VisibilityReferenceResult Early;
		VisibilityReferenceResult Late;
		std::vector<std::uint32_t> Drawn; // Rows drawn by either phase.
	};

	PhaseFrame RunFrame(QuadScene& scene, const RenderViewDesc& view, std::uint32_t size = 64)
	{
		PhaseFrame frame;
		DepthImage depth(size, size, view);
		frame.Early = scene.Run(view, VisibilityPhase::Early);
		scene.Draw(frame.Early, depth);
		const auto hzb = depth.Hzb();
		frame.Late = scene.Run(view, VisibilityPhase::Late, &hzb);
		frame.Drawn = scene.DrawnRows(frame.Early);
		const auto late = scene.DrawnRows(frame.Late);
		frame.Drawn.insert(frame.Drawn.end(), late.begin(), late.end());
		std::sort(frame.Drawn.begin(), frame.Drawn.end());
		return frame;
	}

	bool Contains(const std::vector<std::uint32_t>& rows, std::uint32_t row)
	{
		return std::find(rows.begin(), rows.end(), row) != rows.end();
	}
} // namespace

SWIM_TEST("Render.DepthConvention", "ReverseZIsCanonicalAndItsProjectionsMapNearToOneAndFarToZero")
{
	static_assert(CanonicalDepthConvention == DepthConvention::ReverseZ);
	static_assert(CanonicalDepthFormat == Rhi::Format::D32Float);
	SWIM_CHECK_EQUAL(DepthClearValue(DepthConvention::ReverseZ), 0.0f);
	SWIM_CHECK_EQUAL(DepthClearValue(DepthConvention::Forward), 1.0f);
	SWIM_CHECK(DepthCompareOp(DepthConvention::ReverseZ) == Rhi::CompareOp::GreaterEqual);
	SWIM_CHECK(DepthCompareOp(DepthConvention::Forward) == Rhi::CompareOp::LessEqual);
	SWIM_CHECK_EQUAL(FartherDepth(DepthConvention::ReverseZ, 0.2f, 0.7f), 0.2f);
	SWIM_CHECK_EQUAL(FartherDepth(DepthConvention::Forward, 0.2f, 0.7f), 0.7f);
	SWIM_CHECK(IsNearer(DepthConvention::ReverseZ, 0.7f, 0.2f));
	SWIM_CHECK(IsNearer(DepthConvention::Forward, 0.2f, 0.7f));

	// Orthographic: view z = -near -> 1, -far -> 0.
	const auto ortho = OrthographicReverseZRowMajor(-1, 1, -1, 1, 0.5f, 10.5f);
	const auto nearPoint = Clip(ortho, 0, 0, -0.5f);
	const auto farPoint = Clip(ortho, 0, 0, -10.5f);
	SWIM_CHECK(std::abs(nearPoint[2] / nearPoint[3] - 1.0f) < 1.0e-6f);
	SWIM_CHECK(std::abs(farPoint[2] / farPoint[3]) < 1.0e-6f);

	// Infinite perspective: depth = near / distance.
	const auto perspective = PerspectiveReverseZRowMajor(1.2f, 1.5f, 0.25f);
	for (const float distance : { 0.25f, 1.0f, 40.0f, 1.0e6f })
	{
		const auto clip = Clip(perspective, 0, 0, -distance);
		SWIM_CHECK(std::abs(clip[2] / clip[3] - 0.25f / distance) < 1.0e-6f);
	}
	// Its far plane is degenerate (never culls); the near plane still culls.
	RenderViewDesc desc;
	desc.ViewProjection = perspective;
	const auto view = BuildGpuViewRecord(desc);
	SWIM_CHECK((view.Flags & std::uint32_t(GpuViewFlags::ForwardDepth)) == 0);
	GpuInstanceRecord instance;
	instance.LocalExtents[0] = instance.LocalExtents[1] = instance.LocalExtents[2] = 0.01f;
	GpuTransformRecord transform;
	transform.Current[11] = -1.0e7f;
	SWIM_CHECK(VisibilityMath::InsideFrustum(view, VisibilityMath::WorldSphere(instance, transform)));
	transform.Current[11] = 1.0f; // Behind the camera.
	SWIM_CHECK(!VisibilityMath::InsideFrustum(view, VisibilityMath::WorldSphere(instance, transform)));
	desc.Depth = DepthConvention::Forward;
	SWIM_CHECK((BuildGpuViewRecord(desc).Flags & std::uint32_t(GpuViewFlags::ForwardDepth)) != 0);
	SWIM_CHECK_EQUAL(std::uint32_t(GpuViewFlags::CameraCut), GpuViewFlags::ResetLodHistory | GpuViewFlags::ResetOcclusionHistory);
}

SWIM_TEST("Render.Hzb", "MipChainHalvesRoundingUpAndKeepsTheFarthestDepthOfEachFootprint")
{
	const auto mips = ComputeHzbMips(13, 9);
	SWIM_REQUIRE_EQUAL(mips.size(), 4u);
	SWIM_CHECK(mips[0].Width == 7 && mips[0].Height == 5);
	SWIM_CHECK(mips[1].Width == 4 && mips[1].Height == 3);
	SWIM_CHECK(mips[2].Width == 2 && mips[2].Height == 2);
	SWIM_CHECK(mips[3].Width == 1 && mips[3].Height == 1);
	SWIM_CHECK(ComputeHzbMips(1, 1).empty());
	SWIM_CHECK_EQUAL(ComputeHzbMips(1, 5).size(), 3u); // 1x3, 1x2, 1x1.
	SWIM_CHECK_THROWS(ComputeHzbMips(0, 4), std::invalid_argument);

	std::mt19937 random(7);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (const auto convention : { DepthConvention::ReverseZ, DepthConvention::Forward })
	{
		for (const auto [width, height] : std::array<std::pair<std::uint32_t, std::uint32_t>, 3>{ { { 13, 9 }, { 64, 64 }, { 37, 1 } } })
		{
			std::vector<float> depth(std::size_t(width) * height);
			for (auto& value : depth)
			{
				value = unit(random);
			}
			const auto hzb = HzbReference::Build(depth, width, height, convention);
			SWIM_REQUIRE_EQUAL(hzb.GetMipCount(), std::uint32_t(ComputeHzbMips(width, height).size()));
			// Texel (x, y) of level L equals the farthest depth texel in its footprint.
			for (std::uint32_t mip = 0; mip < hzb.GetMipCount(); ++mip)
			{
				const auto level = mip + 1;
				const auto extent = hzb.GetMipExtent(mip);
				for (std::uint32_t y = 0; y < extent.Height; ++y)
				{
					for (std::uint32_t x = 0; x < extent.Width; ++x)
					{
						float expected = convention == DepthConvention::ReverseZ ? 2.0f : -1.0f;
						for (std::uint32_t sy = y << level; sy < std::min(height, (y + 1) << level); ++sy)
						{
							for (std::uint32_t sx = x << level; sx < std::min(width, (x + 1) << level); ++sx)
							{
								expected = FartherDepth(convention, expected, depth[std::size_t(sy) * width + sx]);
							}
						}
						SWIM_CHECK_EQUAL(hzb.Fetch(mip, x, y), expected);
					}
				}
			}
		}
	}
	const auto tiny = HzbReference::Build(std::vector<float>{ 0.5f, 0.25f }, 2, 1, DepthConvention::ReverseZ);
	SWIM_CHECK_EQUAL(tiny.Fetch(0, 0, 0), 0.25f);
	SWIM_CHECK_THROWS(tiny.Fetch(0, 1, 0), std::out_of_range);
	SWIM_CHECK_THROWS(HzbReference::Build(std::vector<float>(3), 2, 2, DepthConvention::ReverseZ), std::invalid_argument);

	// Adopting read-back mips checks their shapes.
	const auto adopted = HzbReference::FromMips(2, 1, DepthConvention::ReverseZ, { { 0.25f } });
	SWIM_CHECK_EQUAL(adopted.Fetch(0, 0, 0), 0.25f);
	SWIM_CHECK_THROWS(HzbReference::FromMips(4, 4, DepthConvention::ReverseZ, { { 0.0f } }), std::invalid_argument);
	SWIM_CHECK_THROWS(HzbReference::FromMips(2, 1, DepthConvention::ReverseZ, { { 0.0f, 1.0f } }), std::invalid_argument);
}

SWIM_TEST("Render.Occlusion", "HzbTestIsConservativeAgainstFullResolutionDepth")
{
	// Random occluder quads, random spheres: whenever the HZB says "occluded", every
	// depth texel under the sphere's projected rectangle is strictly nearer than the
	// sphere's nearest point. Both conventions; many spheres must actually be occluded.
	std::mt19937 random(11);
	std::uniform_real_distribution<float> position(-9.0f, 9.0f);
	std::uniform_real_distribution<float> size(0.2f, 5.0f);
	std::uniform_real_distribution<float> depthZ(-20.0f, 5.0f);
	for (const auto convention : { DepthConvention::ReverseZ, DepthConvention::Forward })
	{
		const auto desc = OrthoDesc(0, 0, convention);
		const auto view = BuildGpuViewRecord(desc);
		DepthImage depth(96, 80, desc);
		for (int i = 0; i < 12; ++i)
		{
			depth.Quad(position(random), position(random), size(random), size(random), depthZ(random));
		}
		const auto hzb = depth.Hzb();
		const VisibilityMath::HzbDims dims{ hzb.GetWidth(), hzb.GetHeight(), hzb.GetMipCount() };
		std::uint32_t occluded = 0;
		std::uint32_t tested = 0;
		for (int i = 0; i < 3000; ++i)
		{
			VisibilityMath::Sphere sphere;
			sphere.Center = { position(random), position(random), depthZ(random) - 2.0f };
			sphere.Radius = size(random) * 0.5f;
			const bool hidden = VisibilityMath::OccludedByHzb(view, sphere, dims,
				[&](std::uint32_t mip, std::uint32_t x, std::uint32_t y)
				{
					return hzb.Fetch(mip, x, y);
				});
			++tested;
			if (!hidden)
			{
				continue;
			}
			++occluded;
			// Brute force over the sphere AABB's projected pixel rectangle.
			const auto corner = [&](float dx, float dy, float dz)
			{
				const auto clip = Clip(desc.ViewProjection, sphere.Center[0] + dx, sphere.Center[1] + dy, sphere.Center[2] + dz);
				return std::array<float, 3>{ (clip[0] / clip[3] * 0.5f + 0.5f) * float(depth.Width),
					(0.5f - clip[1] / clip[3] * 0.5f) * float(depth.Height), clip[2] / clip[3] };
			};
			const float r = sphere.Radius;
			const auto low = corner(-r, r, r); // Nearest face, top-left.
			const auto high = corner(r, -r, r);
			const float nearest = corner(0, 0, r)[2]; // Orthographic: the sphere's top face is its nearest point.
			bool allNearer = true;
			for (std::uint32_t y = std::uint32_t(std::max(0.0f, std::floor(low[1])));
				 y <= std::uint32_t(std::min(float(depth.Height - 1), high[1])); ++y)
			{
				for (std::uint32_t x = std::uint32_t(std::max(0.0f, std::floor(low[0])));
					 x <= std::uint32_t(std::min(float(depth.Width - 1), high[0])); ++x)
				{
					allNearer = allNearer && IsNearer(convention, depth.Depth[std::size_t(y) * depth.Width + x], nearest);
				}
			}
			SWIM_CHECK(allNearer);
		}
		SWIM_CHECK(occluded > 50u);
		SWIM_CHECK(occluded < tested);
	}
}

SWIM_TEST("Render.Occlusion", "HzbTestRejectsNearPlaneOffscreenUnboundedAndPartialCoverage")
{
	const auto desc = OrthoDesc();
	const auto view = BuildGpuViewRecord(desc);
	DepthImage depth(64, 64, desc);
	depth.Quad(0, 0, 6, 6, 5); // A wall between the camera and z = 0.
	const auto hzb = depth.Hzb();
	const VisibilityMath::HzbDims dims{ hzb.GetWidth(), hzb.GetHeight(), hzb.GetMipCount() };
	const auto occluded = [&](float x, float y, float z, float r, const GpuViewRecord& v)
	{
		VisibilityMath::Sphere sphere;
		sphere.Center = { x, y, z };
		sphere.Radius = r;
		return VisibilityMath::OccludedByHzb(v, sphere, dims,
			[&](std::uint32_t mip, std::uint32_t tx, std::uint32_t ty)
			{
				return hzb.Fetch(mip, tx, ty);
			});
	};
	SWIM_CHECK(occluded(0, 0, 0, 1.4f, view));
	SWIM_CHECK(occluded(2, 2, 0, 1.4f, view));
	SWIM_CHECK(!occluded(0, 0, 6, 0.5f, view));		// In front of the wall.
	SWIM_CHECK(!occluded(5.5f, 0, 0, 1.4f, view));	// Straddles the wall's edge.
	SWIM_CHECK(!occluded(8, 8, 0, 1.0f, view));		// Beside the wall.
	SWIM_CHECK(!occluded(0, 0, 9.95f, 0.5f, view)); // Crosses the near plane.
	SWIM_CHECK(!occluded(40, 0, 0, 1.0f, view));	// Off screen.
	SWIM_CHECK(!VisibilityMath::OccludedByHzb(view, VisibilityMath::Sphere{ { 0, 0, 0 }, 1.0f, true }, dims,
		[&](std::uint32_t mip, std::uint32_t x, std::uint32_t y)
		{
			return hzb.Fetch(mip, x, y);
		}));
	SWIM_CHECK(!VisibilityMath::OccludedByHzb(view, VisibilityMath::Sphere{ { 0, 0, 0 }, 1.0f, false }, VisibilityMath::HzbDims{},
		[](std::uint32_t, std::uint32_t, std::uint32_t)
		{
			return 1.0f;
		}));

	// Perspective: an object behind the camera (w <= 0) is never "occluded".
	RenderViewDesc perspective;
	const std::array<float, 16> lookDown{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -10, 0, 0, 0, 1 };
	perspective.ViewProjection = MultiplyRowMajor(PerspectiveReverseZRowMajor(1.0f, 1.0f, 0.1f), lookDown);
	const auto perspectiveView = BuildGpuViewRecord(perspective);
	SWIM_CHECK(!occluded(0, 0, 12, 1.0f, perspectiveView));
}

SWIM_TEST("Render.Occlusion", "HundredThousandObjectBenchmarkShowsDrawSavings")
{
	// Item 51 benchmark on the CPU definition: 90k small objects in view, 36% of the
	// window behind a wall. After the warm-up frame, occlusion removes the covered
	// objects from both phases; every visible object is still drawn once.
	QuadScene scene;
	scene.Capacity = 1u << 17;
	scene.Add(0, 0, 5, 6, 6);
	constexpr int side = 300;
	for (int j = 0; j < side; ++j)
	{
		for (int i = 0; i < side; ++i)
		{
			scene.Add(-9.9f + float(i) * 0.066f, -9.9f + float(j) * 0.066f, 0, 0.02f, 0.02f);
		}
	}
	const auto view = OrthoDesc();
	RunFrame(scene, view, 256);
	RunFrame(scene, view, 256);
	const auto start = std::chrono::steady_clock::now();
	const auto frame = RunFrame(scene, view, 256);
	const auto end = std::chrono::steady_clock::now();
	const auto total = std::uint32_t(scene.Instances.size());
	const auto drawn = std::uint32_t(frame.Drawn.size());
	SWIM_CHECK_EQUAL(frame.Early.Stats.Tested, total);
	SWIM_CHECK_EQUAL(frame.Late.Stats.Visible, 0u);
	SWIM_CHECK_EQUAL(drawn + frame.Late.Stats.Occluded, total);
	SWIM_CHECK(frame.Late.Stats.Occluded > total * 3 / 10); // Most of the ~36% behind the wall.
	SWIM_CHECK(frame.Late.Stats.Occluded < total * 4 / 10);
	std::printf("             [Occlusion benchmark] %u objects: %u drawn, %u occluded (%.1f%% saved), two-phase CPU frame %.1f ms\n", total,
		drawn, frame.Late.Stats.Occluded, 100.0 * double(frame.Late.Stats.Occluded) / double(total),
		std::chrono::duration<double, std::milli>(end - start).count());
}

SWIM_TEST("Render.Visibility", "TwoPhaseOcclusionDrawsLastVisibleEarlyAndRevealsNewlyVisibleLate")
{
	QuadScene scene;
	const auto wall = scene.Add(0, 0, 5, 6, 6);
	std::vector<std::uint32_t> hidden;
	std::vector<std::uint32_t> open;
	for (int j = -3; j <= 3; ++j)
	{
		for (int i = -3; i <= 3; ++i)
		{
			const auto row = scene.Add(float(i) * 2.8f, float(j) * 2.8f, 0, 1, 1);
			(std::abs(i) <= 1 && std::abs(j) <= 1 ? hidden : open).push_back(row);
		}
	}
	const auto outside = scene.Add(50, 0, 0, 1, 1);
	const std::uint32_t inFrustum = static_cast<std::uint32_t>(scene.Instances.size()) - 1;
	const auto view = OrthoDesc();

	// Frame 1 (no history): the early phase draws nothing; the HZB is empty, so the
	// late phase draws every in-frustum object and records them as visible.
	auto frame = RunFrame(scene, view);
	SWIM_CHECK_EQUAL(frame.Early.Stats.Visible, 0u);
	SWIM_CHECK_EQUAL(frame.Early.Stats.Deferred, inFrustum);
	SWIM_CHECK_EQUAL(frame.Early.Stats.FrustumCulled, 1u);
	SWIM_CHECK_EQUAL(frame.Late.Stats.Visible, inFrustum);
	SWIM_CHECK_EQUAL(frame.Late.Stats.Occluded, 0u);
	SWIM_CHECK_EQUAL(frame.Drawn.size(), std::size_t(inFrustum));
	SWIM_CHECK(!Contains(frame.Drawn, outside));

	// Frame 2: everything is drawn early; the late test (against that depth) finds
	// the objects behind the wall occluded and drops them from the history.
	frame = RunFrame(scene, view);
	SWIM_CHECK_EQUAL(frame.Early.Stats.Visible, inFrustum);
	SWIM_CHECK_EQUAL(frame.Late.Stats.AlreadyDrawn, inFrustum);
	SWIM_CHECK_EQUAL(frame.Late.Stats.Visible, 0u);
	for (const auto row : hidden)
	{
		SWIM_CHECK_EQUAL(scene.History[row], 0u);
	}

	// Frame 3: steady state. Hidden objects are neither drawn early nor late.
	frame = RunFrame(scene, view);
	SWIM_CHECK_EQUAL(frame.Early.Stats.Visible, inFrustum - std::uint32_t(hidden.size()));
	SWIM_CHECK_EQUAL(frame.Early.Stats.Deferred, std::uint32_t(hidden.size()));
	SWIM_CHECK_EQUAL(frame.Late.Stats.Occluded, std::uint32_t(hidden.size()));
	SWIM_CHECK_EQUAL(frame.Late.Stats.Visible, 0u);
	for (const auto row : hidden)
	{
		SWIM_CHECK(!Contains(frame.Drawn, row));
	}
	for (const auto row : open)
	{
		SWIM_CHECK(Contains(frame.Drawn, row));
	}
	const auto& late = frame.Late.Stats;
	SWIM_CHECK_EQUAL(late.FrustumCulled + late.NotDrawable + late.Visible + late.AlreadyDrawn + late.Occluded, late.Tested);
	const auto& early = frame.Early.Stats;
	SWIM_CHECK_EQUAL(early.FrustumCulled + early.NotDrawable + early.Visible + early.Deferred, early.Tested);

	// Frame 4: the wall teleports away. The early phase draws the old visible set (the
	// wall at its new place); the revealed objects are found by the late phase in the
	// same frame, so nothing is hidden by stale history.
	scene.Move(wall, 30, 0, 5);
	frame = RunFrame(scene, view);
	SWIM_CHECK_EQUAL(frame.Late.Stats.Visible, std::uint32_t(hidden.size()));
	for (const auto row : hidden)
	{
		SWIM_CHECK(Contains(frame.Drawn, row));
	}

	// Wall back, then a camera cut: history is ignored, so the early phase draws every
	// in-frustum object (conservative) and the late phase adds nothing.
	scene.Move(wall, 0, 0, 5);
	RunFrame(scene, view);
	RunFrame(scene, view);
	frame = RunFrame(scene, OrthoDesc(0, 0, DepthConvention::ReverseZ, std::uint32_t(GpuViewFlags::CameraCut)));
	SWIM_CHECK_EQUAL(frame.Early.Stats.Visible, inFrustum);
	SWIM_CHECK_EQUAL(frame.Late.Stats.AlreadyDrawn, inFrustum);
	SWIM_CHECK_EQUAL(frame.Late.Stats.Visible, 0u);

	// DisableOcclusion: the late phase draws every remaining in-frustum object.
	RunFrame(scene, view);
	frame = RunFrame(scene, OrthoDesc(0, 0, DepthConvention::ReverseZ, std::uint32_t(GpuViewFlags::DisableOcclusion)));
	SWIM_CHECK_EQUAL(frame.Late.Stats.Occluded, 0u);
	SWIM_CHECK_EQUAL(frame.Drawn.size(), std::size_t(inFrustum));

	// A reused row (new generation) has no history.
	scene.Instances[open[0]].Generation = 2;
	frame = RunFrame(scene, view);
	SWIM_CHECK_EQUAL(frame.Early.Stats.Deferred, 1u);
	SWIM_CHECK(Contains(frame.Drawn, open[0]));

	// Invalid inputs.
	SWIM_CHECK_THROWS(scene.Run(view, VisibilityPhase::Late), std::invalid_argument);
	const DepthImage forward(8, 8, OrthoDesc(0, 0, DepthConvention::Forward));
	const auto forwardHzb = forward.Hzb();
	SWIM_CHECK_THROWS(scene.Run(view, VisibilityPhase::Late, &forwardHzb), std::invalid_argument);
	std::vector<GpuLodState> lods;
	const VisibilityBinLayout bins(std::vector<std::uint32_t>{ 4 }, 1);
	VisibilityReferenceInputs inputs{ scene.Instances, scene.Transforms, scene.Meshes, scene.Submeshes, BuildGpuViewRecord(view), {}, {},
		&bins };
	inputs.Phase = VisibilityPhase::Early;
	SWIM_CHECK_THROWS(RunVisibilityReference(inputs, lods), std::invalid_argument);
}

namespace
{
	struct HzbWorld
	{
		HzbWorld()
		{
			device.CreateTextures = true;
			executor = std::make_unique<RenderGraphExecutor>(device);
			Rhi::DescriptorSchemaDesc schema{ 0, {} };
			schema.Bindings.push_back({ HzbBindings::Source, Rhi::DescriptorType::SampledTexture, 1, Rhi::ShaderStageMask::Compute });
			schema.Bindings.push_back({ HzbBindings::Destination, Rhi::DescriptorType::StorageTexture, 1, Rhi::ShaderStageMask::Compute });
			layout.program.Interface.DescriptorSchemas = { schema };
		}

		GraphTexture Depth(RenderGraph& graph, std::uint32_t width, std::uint32_t height)
		{
			Rhi::TextureDesc desc;
			desc.Extent = { width, height, 1 };
			desc.PixelFormat = Rhi::Format::D32Float;
			desc.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled;
			desc.DebugName = "Depth";
			const auto depth = graph.CreateTexture(desc);
			graph.AddPass(
				"Depth writer", Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					b.Write(depth, Rhi::ResourceState::DepthStencilWrite);
				},
				[](RenderCommandContext&)
				{
				});
			return depth;
		}

		std::vector<Testing::MockCommand> Commands(const std::string& kind) const
		{
			std::vector<Testing::MockCommand> result;
			for (const auto& command : *device.Commands)
			{
				if (command.Kind == kind)
				{
					result.push_back(command);
				}
			}
			return result;
		}

		Testing::MockDevice device;
		Testing::MockPipelineLayout layout;
		Testing::MockComputePipeline pipeline;
		std::unique_ptr<RenderGraphExecutor> executor;
	};
} // namespace

SWIM_TEST("Render.Hzb", "BuilderRecordsOneGraphReductionPerMip")
{
	HzbWorld world;
	SWIM_CHECK_THROWS(HzbBuilder(HzbBuilderDesc{}), std::invalid_argument);
	const HzbBuilder builder({ &world.pipeline, &world.layout, 0, "Test HZB" });
	RenderGraph graph;
	const auto depth = world.Depth(graph, 13, 9);
	const auto hzb = builder.Record(graph, depth);
	SWIM_CHECK_EQUAL(hzb.Width, 13u);
	SWIM_CHECK_EQUAL(hzb.Height, 9u);
	SWIM_CHECK_EQUAL(hzb.MipCount, 4u);
	SWIM_CHECK_EQUAL(hzb.Passes.size(), 4u);
	SWIM_CHECK(hzb.Convention == DepthConvention::ReverseZ);
	const auto& pyramid = graph.GetDesc(hzb.Pyramid);
	SWIM_CHECK(pyramid.PixelFormat == Rhi::Format::R32Float);
	SWIM_CHECK(pyramid.Extent.Width == 7 && pyramid.Extent.Height == 5 && pyramid.MipLevels == 4);
	graph.Export(hzb.Pyramid, Rhi::ResourceState::ShaderRead);
	world.executor->Execute(graph.Compile());
	world.executor->Wait();

	// One dispatch per mip, 8x8 groups over the destination; constants name both sizes.
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), 4u);
	SWIM_CHECK(dispatches[0].SourceOffset == 1 && dispatches[0].DestinationOffset == 1 && dispatches[0].Size == 1);
	const auto constants = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(constants.size(), 4u);
	const std::array<std::array<std::uint32_t, 5>, 4> expected{ { { 13, 9, 7, 5, 0 }, { 7, 5, 4, 3, 0 }, { 4, 3, 2, 2, 0 },
		{ 2, 2, 1, 1, 0 } } };
	for (std::size_t mip = 0; mip < 4; ++mip)
	{
		std::array<std::uint32_t, 5> values{};
		SWIM_REQUIRE_EQUAL(constants[mip].Data.size(), sizeof(values));
		std::memcpy(values.data(), constants[mip].Data.data(), sizeof(values));
		SWIM_CHECK(values == expected[mip]);
	}
	// The last pass reads mip 2 and writes mip 3 through single-mip views.
	const auto* table = world.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	const auto* source = static_cast<const Rhi::TextureView*>(table->Element(HzbBindings::Source, 0));
	const auto* destination = static_cast<const Rhi::TextureView*>(table->Element(HzbBindings::Destination, 0));
	SWIM_CHECK_EQUAL(source->GetDesc().BaseMipLevel, 2u);
	SWIM_CHECK_EQUAL(destination->GetDesc().BaseMipLevel, 3u);
	SWIM_CHECK(destination->GetDesc().PixelFormat == Rhi::Format::R32Float);

	// Forward depth keeps the maximum.
	RenderGraph forwardGraph;
	const auto forward = builder.Record(forwardGraph, world.Depth(forwardGraph, 2, 2), DepthConvention::Forward);
	SWIM_CHECK_EQUAL(forward.MipCount, 1u);
	forwardGraph.Export(forward.Pyramid, Rhi::ResourceState::ShaderRead);
	world.executor->Execute(forwardGraph.Compile());
	world.executor->Wait();
	const auto forwardConstants = world.Commands("PushConstants");
	std::uint32_t mode = 0;
	std::memcpy(&mode, forwardConstants.back().Data.data() + 16, sizeof(mode));
	SWIM_CHECK_EQUAL(mode, 1u);

	// Rejected sources: 1x1, color formats other than R32Float, unsampled.
	RenderGraph bad;
	SWIM_CHECK_THROWS(builder.Record(bad, world.Depth(bad, 1, 1)), std::invalid_argument);
	Rhi::TextureDesc color;
	color.Extent = { 8, 8, 1 };
	color.PixelFormat = Rhi::Format::RGBA8Unorm;
	color.Usage = Rhi::TextureUsage::Sampled;
	SWIM_CHECK_THROWS(builder.Record(bad, bad.CreateTexture(color)), std::invalid_argument);
	color.PixelFormat = Rhi::Format::D32Float;
	color.Usage = Rhi::TextureUsage::DepthStencilAttachment;
	SWIM_CHECK_THROWS(builder.Record(bad, bad.CreateTexture(color)), std::invalid_argument);
}
