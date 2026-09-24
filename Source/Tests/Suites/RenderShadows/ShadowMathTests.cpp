#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Shadows/ShadowMath.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/ShadowFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <random>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace Sh = Swim::Render::Shadows;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	Sh::ShadowCamera Camera(const Sh::Float3& eye, const Sh::Float3& target)
	{
		Sh::ShadowCamera camera;
		camera.View = Scene::LookAt(eye, target, { 0, 1, 0 });
		camera.VerticalFov = 0.9f;
		camera.Aspect = 16.0f / 9.0f;
		camera.Near = 0.1f;
		return camera;
	}

	float Distance(const Sh::Float3& a, const Sh::Float3& b)
	{
		return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
	}

	// World point of a camera-view point (inverse of the affine look-at).
	Sh::Float3 ViewToWorld(const Sh::Matrix& view, const Sh::Float3& v)
	{
		const Sh::Float3 d{ v[0] - view[3], v[1] - view[7], v[2] - view[11] };
		return { view[0] * d[0] + view[4] * d[1] + view[8] * d[2], view[1] * d[0] + view[5] * d[1] + view[9] * d[2],
			view[2] * d[0] + view[6] * d[1] + view[10] * d[2] };
	}

	// Light-clip x, y (NDC) of a world point under an orthographic view-projection.
	std::array<float, 3> Clip(const Sh::Matrix& m, const Sh::Float3& p)
	{
		std::array<float, 4> clip{};
		for (int r = 0; r < 4; ++r)
		{
			clip[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
		}
		return { clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3] };
	}
} // namespace

SWIM_TEST("Render.Shadows.Math", "CascadeSplitsBlendUniformAndLogarithmic")
{
	const auto uniform = Sh::CascadeSplits(1.0f, 101.0f, 4, 0.0f);
	SWIM_CHECK((uniform == std::vector<float>{ 1.0f, 26.0f, 51.0f, 76.0f, 101.0f }));
	const auto logarithmic = Sh::CascadeSplits(1.0f, 81.0f, 4, 1.0f);
	SWIM_CHECK(std::abs(logarithmic[1] - 3.0f) < 1.0e-4f && std::abs(logarithmic[2] - 9.0f) < 1.0e-3f &&
		std::abs(logarithmic[3] - 27.0f) < 1.0e-3f);
	const auto practical = Sh::CascadeSplits(0.1f, 60.0f, 3, 0.75f);
	for (std::size_t i = 1; i < practical.size(); ++i)
	{
		SWIM_CHECK(practical[i] > practical[i - 1]);
	}
	SWIM_CHECK(practical.front() == 0.1f && practical.back() == 60.0f);
	SWIM_CHECK_THROWS(Sh::CascadeSplits(0.0f, 10.0f, 2, 0.5f), std::invalid_argument);
	SWIM_CHECK_THROWS(Sh::CascadeSplits(1.0f, 10.0f, 5, 0.5f), std::invalid_argument);
	SWIM_CHECK_THROWS(Sh::CascadeSplits(1.0f, 10.0f, 2, 1.5f), std::invalid_argument);
}

SWIM_TEST("Render.Shadows.Math", "CascadeSpheresContainTheirSlicesAndIgnoreCameraRotation")
{
	std::mt19937 random(70);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	float firstRadius = -1.0f;
	for (int trial = 0; trial < 50; ++trial)
	{
		const Sh::Float3 eye{ 10 * unit(random), 2 + 3 * unit(random), 10 * unit(random) };
		const Sh::Float3 target{ eye[0] + unit(random), eye[1] + 0.5f * unit(random), eye[2] + unit(random) };
		const auto camera = Camera(eye, target);
		const auto sphere = Sh::CascadeSphere(camera, 4.0f, 15.0f);
		// Every corner of the slice is inside (to float precision).
		const float tanHalf = std::tan(camera.VerticalFov * 0.5f);
		for (const float depth : { 4.0f, 15.0f })
		{
			for (const float sx : { -1.0f, 1.0f })
			{
				for (const float sy : { -1.0f, 1.0f })
				{
					const auto corner = ViewToWorld(camera.View, { sx * depth * tanHalf * camera.Aspect, sy * depth * tanHalf, -depth });
					SWIM_CHECK(Distance(corner, sphere.Center) <= sphere.Radius * (1.0f + 1.0e-5f));
				}
			}
		}
		// Rotation and translation invariant radius: no shimmer from sphere size.
		if (firstRadius < 0.0f)
		{
			firstRadius = sphere.Radius;
		}
		SWIM_CHECK(std::abs(sphere.Radius - firstRadius) < 1.0e-5f * firstRadius);
	}
	// A slice wider than it is deep centers on its far plane.
	Sh::ShadowCamera wide = Camera({ 0, 0, 0 }, { 0, 0, -1 });
	wide.VerticalFov = 2.8f;
	const auto farSphere = Sh::CascadeSphere(wide, 1.0f, 2.0f);
	SWIM_CHECK(std::abs(Distance(farSphere.Center, { 0, 0, -2 })) < 1.0e-5f);
}

SWIM_TEST("Render.Shadows.Math", "CascadesSnapToWholeTexelsAsTheCameraMoves")
{
	Sh::CascadeSettings settings;
	settings.Count = 3;
	settings.MaxDistance = 40.0f;
	const Sh::Float3 light{ -0.4f, -1.0f, -0.3f };
	constexpr std::uint32_t resolution = 512;
	const auto reference = Sh::ComputeCascades(Camera({ 0, 4, 12 }, { 0, 1, 0 }), light, settings, resolution);
	SWIM_REQUIRE_EQUAL(reference.size(), std::size_t(3));
	SWIM_CHECK(reference[0].Near == 0.1f && reference[2].Far == 40.0f);
	for (int step = 1; step <= 25; ++step)
	{
		// Sub-texel camera motion.
		const float d = 0.013f * float(step);
		const auto moved = Sh::ComputeCascades(Camera({ d, 4 + 0.5f * d, 12 - d }, { d, 1 + 0.5f * d, -d }), light, settings, resolution);
		for (std::size_t i = 0; i < moved.size(); ++i)
		{
			SWIM_CHECK(moved[i].TexelWorldSize == reference[i].TexelWorldSize);
			// A fixed world point lands on the same sub-texel position in every frame:
			// the cascade moved by whole texels only.
			for (const Sh::Float3 probe : { Sh::Float3{ 0.3f, 0.7f, -1.1f }, Sh::Float3{ 2.0f, 0.0f, 3.0f } })
			{
				const auto a = Clip(reference[i].ViewProjection, probe);
				const auto b = Clip(moved[i].ViewProjection, probe);
				for (int axis = 0; axis < 2; ++axis)
				{
					const float texels = (a[axis] - b[axis]) * 0.5f * float(resolution);
					SWIM_CHECK(std::abs(texels - std::round(texels)) < 2.0e-2f);
				}
			}
		}
	}
	// Every point of each cascade's sphere projects inside the view, including the
	// caster extension toward the light.
	for (const auto& cascade : reference)
	{
		for (const Sh::Float3 offset :
			{ Sh::Float3{ 1, 0, 0 }, Sh::Float3{ 0, 1, 0 }, Sh::Float3{ 0, 0, 1 }, Sh::Float3{ -0.577f, 0.577f, 0.577f } })
		{
			for (const float sign : { -1.0f, 1.0f })
			{
				const float r = cascade.Sphere.Radius * 0.999f;
				const Sh::Float3 p{ cascade.Sphere.Center[0] + sign * r * offset[0], cascade.Sphere.Center[1] + sign * r * offset[1],
					cascade.Sphere.Center[2] + sign * r * offset[2] };
				const auto c = Clip(cascade.ViewProjection, p);
				SWIM_CHECK(std::abs(c[0]) <= 1.0f && std::abs(c[1]) <= 1.0f && c[2] >= 0.0f && c[2] <= 1.0f);
			}
		}
		// A caster 40 m toward the light from the sphere center is still in range (reverse-Z: nearer).
		const auto normalizedLight = StandardPbr::Normalize(light);
		const Sh::Float3 caster{ cascade.Sphere.Center[0] - 40.0f * normalizedLight[0],
			cascade.Sphere.Center[1] - 40.0f * normalizedLight[1], cascade.Sphere.Center[2] - 40.0f * normalizedLight[2] };
		const auto c = Clip(cascade.ViewProjection, caster);
		SWIM_CHECK(c[2] > Clip(cascade.ViewProjection, cascade.Sphere.Center)[2] && c[2] <= 1.0f);
	}
}

SWIM_TEST("Render.Shadows.Math", "SpotAndPointViewsCoverTheirLights")
{
	LightDesc desc;
	desc.Type = LightType::Spot;
	desc.Position = { 1, 5, 2 };
	desc.Direction = { 0.2f, -1.0f, 0.1f };
	desc.Range = 12.0f;
	desc.OuterConeAngle = 0.6f;
	desc.InnerConeAngle = 0.3f;
	const auto spot = Lights::EncodeLight(desc);
	SWIM_CHECK(std::abs(Sh::SpotShadowFov(spot) - 1.2f) < 1.0e-4f);
	const auto spotMatrix = Sh::SpotShadowViewProjection(spot, 0.05f);
	GpuShadowView spotView;
	std::copy(spotMatrix.begin(), spotMatrix.end(), spotView.ViewProjection);
	spotView.AtlasRect[2] = spotView.AtlasRect[3] = 512.0f;
	std::mt19937 random(71);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::uint32_t inside = 0;
	for (int i = 0; i < 2000; ++i)
	{
		// Points inside the cone (angle < outer) and range project into the view.
		const Sh::Float3 p{ desc.Position[0] + 20 * unit(random) - 10, desc.Position[1] - 12 * unit(random),
			desc.Position[2] + 20 * unit(random) - 10 };
		const Sh::Float3 d{ p[0] - desc.Position[0], p[1] - desc.Position[1], p[2] - desc.Position[2] };
		const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		const float cosAngle = (d[0] * spot.Direction[0] + d[1] * spot.Direction[1] + d[2] * spot.Direction[2]) / length;
		if (length < 0.1f || length > desc.Range || cosAngle < std::cos(desc.OuterConeAngle) * 1.0001f)
		{
			continue;
		}
		++inside;
		const auto projected = Sh::ProjectToShadowView(spotView, p);
		SWIM_CHECK(projected.has_value());
		if (projected)
		{
			// Reverse-Z infinite: depth = near / view depth.
			SWIM_CHECK(projected->Depth > 0.0f && projected->Depth < 1.0f);
		}
	}
	SWIM_CHECK(inside > 200u);
	// Wide cones are clipped to MaxSpotShadowFov.
	desc.OuterConeAngle = 1.5f;
	SWIM_CHECK(Sh::SpotShadowFov(Lights::EncodeLight(desc)) == Sh::MaxSpotShadowFov);

	// Point: every direction's major-axis face contains it.
	const Sh::Float3 center{ -2, 3, 1 };
	const auto faces = Sh::PointShadowViewProjections(center, 0.05f);
	for (int i = 0; i < 3000; ++i)
	{
		const Sh::Float3 d{ 2 * unit(random) - 1, 2 * unit(random) - 1, 2 * unit(random) - 1 };
		const float scale = 0.5f + 5.0f * unit(random);
		const Sh::Float3 p{ center[0] + d[0] * scale, center[1] + d[1] * scale, center[2] + d[2] * scale };
		const auto face = Sh::PointShadowFace(d);
		GpuShadowView view;
		std::copy(faces[face].begin(), faces[face].end(), view.ViewProjection);
		view.AtlasRect[2] = view.AtlasRect[3] = 64.0f;
		SWIM_CHECK(Sh::ProjectToShadowView(view, p).has_value());
		// And the opposite face never does.
		GpuShadowView opposite;
		std::copy(faces[face ^ 1u].begin(), faces[face ^ 1u].end(), opposite.ViewProjection);
		opposite.AtlasRect[2] = opposite.AtlasRect[3] = 64.0f;
		SWIM_CHECK(!Sh::ProjectToShadowView(opposite, p).has_value());
	}
	SWIM_CHECK_EQUAL(Sh::PointShadowFace({ 1, 1, 1 }), 0u); // Ties go to X, then Y.
	SWIM_CHECK_EQUAL(Sh::PointShadowFace({ 0, -1, 1 }), 3u);
	SWIM_CHECK_EQUAL(Sh::PointShadowFace({ 0, 0.2f, -1 }), 5u);
}

SWIM_TEST("Render.Shadows.Math", "ProjectionMapsTilesTopDownAndInvertsExactly")
{
	GpuShadowView view;
	const auto matrix = OrthographicReverseZRowMajor(-2, 2, -2, 2, 0, 10); // Looking down -Z from the origin.
	std::copy(matrix.begin(), matrix.end(), view.ViewProjection);
	view.AtlasRect[0] = 256;
	view.AtlasRect[1] = 128;
	view.AtlasRect[2] = view.AtlasRect[3] = 64;
	const auto topLeft = Sh::ProjectToShadowView(view, { -2.0f, 2.0f, -5.0f });
	SWIM_REQUIRE(topLeft.has_value());
	SWIM_CHECK(topLeft->PixelX == 256.0f && topLeft->PixelY == 128.0f);
	SWIM_CHECK(std::abs(topLeft->Depth - 0.5f) < 1.0e-6f);
	const auto center = Sh::ProjectToShadowView(view, { 0.0f, 0.0f, -10.0f });
	SWIM_REQUIRE(center.has_value());
	SWIM_CHECK(center->PixelX == 288.0f && center->PixelY == 160.0f && center->Depth == 0.0f);
	SWIM_CHECK(!Sh::ProjectToShadowView(view, { 0.0f, 0.0f, -10.5f }).has_value()); // Beyond far.
	SWIM_CHECK(!Sh::ProjectToShadowView(view, { 2.1f, 0.0f, -5.0f }).has_value());	// Outside the tile.

	const auto inverse = Testing::ShadowScene::Inverse(matrix);
	SWIM_REQUIRE(inverse.has_value());
	const auto back = Testing::ShadowScene::Unproject(*inverse, 0.5f, -0.25f, 0.3f);
	const auto again = Clip(matrix, back);
	SWIM_CHECK(std::abs(again[0] - 0.5f) < 1.0e-5f && std::abs(again[1] + 0.25f) < 1.0e-5f && std::abs(again[2] - 0.3f) < 1.0e-5f);
}
