#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Fixtures/ScreenSpaceFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace Ss = Swim::Render::ScreenSpace;
namespace Scene = Swim::Testing::ScreenSpaceScene;

namespace
{
	constexpr std::uint32_t Width = 160;
	constexpr std::uint32_t Height = 90;

	// A floor with a tall wall filling x < 0, seen from the open side.
	Scene::Scene WallScene()
	{
		Scene::Scene scene;
		scene.Boxes.push_back({ { -20.0f, 0.0f, -20.0f }, { 0.0f, 10.0f, 20.0f } });
		return scene;
	}

	ScreenSpaceView WallView()
	{
		return Scene::View({ 3.0f, 2.0f, 6.0f }, { 1.0f, 0.0f, 0.0f }, float(Width) / float(Height));
	}

	struct Band
	{
		double Sum = 0.0;
		float Minimum = 1.0f;
		std::uint32_t Count = 0;

		double Mean() const { return Count ? Sum / Count : 0.0; }
	};

	// Mean visibility of the floor pixels whose distance to the wall lies in [from, to).
	Band FloorBand(const Scene::Inputs& inputs, const Ss::ScalarImage& ao, float from, float to)
	{
		Band band;
		for (std::uint32_t y = 0; y < Height; ++y)
		{
			for (std::uint32_t x = 0; x < Width; ++x)
			{
				const auto& hit = inputs.Hits[std::size_t(y) * Width + x];
				if (hit.T > 0.0f && hit.Normal[1] > 0.5f && hit.Position[0] >= from && hit.Position[0] < to &&
					std::abs(hit.Position[2]) < 4.0f)
				{
					band.Sum += ao.At(x, y);
					band.Minimum = std::min(band.Minimum, ao.At(x, y));
					++band.Count;
				}
			}
		}
		return band;
	}

	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}
} // namespace

SWIM_TEST("Render.ScreenSpace.Reference", "ParamsPackTheViewAndValidate")
{
	auto view = WallView();
	view.Jitter = { 0.01f, -0.02f };
	ScreenSpaceSettings settings;
	settings.Fog.SunDirection = { 0.0f, -2.0f, 0.0f };
	const auto params = BuildScreenSpaceParams(settings, view, Width, Height, 70);
	// InverseProjection * Projection = identity.
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k)
			{
				sum += params.InverseProjection[r * 4 + k] * view.Projection[k * 4 + c];
			}
			SWIM_CHECK(Near(sum, r == c ? 1.0f : 0.0f, 1.0e-5f));
		}
	}
	SWIM_CHECK(Near(params.InverseViewRows[3], 3.0f, 1.0e-5f)); // The camera position.
	SWIM_CHECK(Near(params.InverseViewRows[7], 2.0f, 1.0e-5f) && Near(params.InverseViewRows[11], 6.0f, 1.0e-5f));
	SWIM_CHECK(params.ViewRows[0] == view.View[0] && params.ViewRows[11] == view.View[11]);
	SWIM_CHECK(params.Jitter[0] == 0.01f && params.Jitter[1] == -0.02f);
	SWIM_CHECK(Near(params.AoRadiusToPixels, view.Projection[0] * Width * 0.5f, 1.0e-4f));
	SWIM_CHECK_EQUAL(params.NoiseFrame, 6u); // 70 mod 64.
	SWIM_CHECK(params.AoEnabled == 1u && params.FogEnabled == 0u);
	SWIM_CHECK(params.FogSunDirection[1] == -1.0f); // Normalized.
	SWIM_CHECK(params.Width == Width && params.Height == Height);

	const auto rejects = [&](const std::function<void(ScreenSpaceSettings&, ScreenSpaceView&)>& edit)
	{
		auto s = ScreenSpaceSettings{};
		auto v = WallView();
		edit(s, v);
		bool threw = false;
		try
		{
			BuildScreenSpaceParams(s, v, Width, Height, 0);
		}
		catch (const std::invalid_argument&)
		{
			threw = true;
		}
		return threw;
	};
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.Radius = 0.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.Falloff = 1.5f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.Power = 0.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.MaxRadiusPixels = 0.5f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.SliceCount = MaxAoSlices + 1;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.StepCount = 0;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.AmbientOcclusion.BlurDepthTolerance = 0.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.Density = -1.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.HeightFalloff = NAN;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.Color[1] = -0.1f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.SunColor[2] = INFINITY;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.SunDirection = { 0, 0, 0 };
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.Anisotropy = 0.99f;
		}));
	SWIM_CHECK(rejects(
		[](auto& s, auto&)
		{
			s.Fog.MaxDistance = s.Fog.StartDistance;
		}));
	SWIM_CHECK(rejects(
		[](auto&, auto& v)
		{
			v.Jitter[0] = NAN;
		}));
	SWIM_CHECK(rejects(
		[](auto&, auto& v)
		{
			v.Projection[14] = 0.0f;
			v.Projection[15] = 1.0f;
		})); // Orthographic.
	SWIM_CHECK(rejects(
		[](auto&, auto& v)
		{
			v.View = {};
		})); // Singular.
	SWIM_CHECK_THROWS(BuildScreenSpaceParams(ScreenSpaceSettings{}, WallView(), 0, Height, 0), std::invalid_argument);
	SWIM_CHECK(!Ss::Inverse(std::array<float, 16>{}).has_value());
}

SWIM_TEST("Render.ScreenSpace.Reference", "ViewPositionsInvertTheJitteredProjection")
{
	auto view = WallView();
	view.Jitter = { 0.004f, -0.003f };
	const auto params = BuildScreenSpaceParams(ScreenSpaceSettings{}, view, Width, Height, 0);
	std::mt19937 random(76);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (int i = 0; i < 200; ++i)
	{
		const Ss::Float3 point{ 8.0f * unit(random) - 4.0f, 6.0f * unit(random) - 3.0f, -0.5f - 40.0f * unit(random) };
		std::array<float, 4> clip{};
		for (int r = 0; r < 4; ++r)
		{
			clip[r] = view.Projection[r * 4] * point[0] + view.Projection[r * 4 + 1] * point[1] + view.Projection[r * 4 + 2] * point[2] +
				view.Projection[r * 4 + 3];
		}
		// The jittered raster places the point at NDC + jitter.
		const float px = (clip[0] / clip[3] + view.Jitter[0] + 1.0f) * 0.5f * Width;
		const float py = (1.0f - (clip[1] / clip[3] + view.Jitter[1])) * 0.5f * Height;
		const auto back = Ss::ViewPosition(params, px, py, clip[2] / clip[3]);
		SWIM_REQUIRE(back.has_value());
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(Near((*back)[c], point[c], 2.0e-4f * std::abs(point[2])));
		}
	}
	SWIM_CHECK(!Ss::ViewPosition(params, 10.0f, 10.0f, 0.0f).has_value()); // Sky.
	const auto up = Ss::ViewNormal(params, { 0.0f, 3.0f, 0.0f });
	SWIM_REQUIRE(up.has_value());
	SWIM_CHECK(Near((*up)[0] * (*up)[0] + (*up)[1] * (*up)[1] + (*up)[2] * (*up)[2], 1.0f, 1.0e-5f));
	SWIM_CHECK((*up)[1] > 0.5f); // The camera looks down at the floor: world up stays mostly up.
	SWIM_CHECK(!Ss::ViewNormal(params, { 0, 0, 0 }).has_value());
	for (std::uint32_t frame = 0; frame < 3; ++frame)
	{
		const float noise = Ss::InterleavedGradientNoise(3.0f, 4.0f, frame);
		SWIM_CHECK(noise >= 0.0f && noise < 1.0f);
	}
	SWIM_CHECK(Ss::InterleavedGradientNoise(3.0f, 4.0f, 1) != Ss::InterleavedGradientNoise(3.0f, 4.0f, 2));
	SWIM_CHECK(Ss::InterleavedGradientNoise(3.0f, 4.0f, 65) == Ss::InterleavedGradientNoise(3.0f, 4.0f, 1));
}

SWIM_TEST("Render.ScreenSpace.Reference", "GtaoIsOpenOnFlatSurfacesAndDarkensTowardCreases")
{
	const auto scene = WallScene();
	const auto view = WallView();
	const auto inputs = Scene::Render(scene, view, Width, Height);
	ScreenSpaceSettings settings;
	settings.AmbientOcclusion.Radius = 1.0f;
	Band crease, near, far, wall;
	for (std::uint32_t frame = 0; frame < 4; ++frame)
	{
		const auto params = BuildScreenSpaceParams(settings, view, Width, Height, frame);
		const auto raw = Ss::Gtao(params, inputs.Depth, inputs.Normal);
		const auto ao = Ss::Blur(params, raw, inputs.Depth);
		const auto add = [](Band& total, const Band& band)
		{
			total.Sum += band.Sum;
			total.Count += band.Count;
			total.Minimum = std::min(total.Minimum, band.Minimum);
		};
		add(crease, FloorBand(inputs, ao, 0.0f, 0.1f));
		add(near, FloorBand(inputs, ao, 0.4f, 0.6f));
		add(far, FloorBand(inputs, ao, 1.3f, 2.5f)); // Beyond the radius: nothing occludes.
		for (std::uint32_t y = 0; y < Height; ++y)
		{
			for (std::uint32_t x = 0; x < Width; ++x)
			{
				const auto& hit = inputs.Hits[std::size_t(y) * Width + x];
				if (hit.T == 0.0f)
				{
					SWIM_CHECK(raw.At(x, y) == 1.0f && ao.At(x, y) == 1.0f); // Sky.
				}
				else if (hit.Normal[0] > 0.5f && hit.Position[1] > 1.5f && hit.Position[1] < 4.0f)
				{
					wall.Sum += ao.At(x, y);
					wall.Minimum = std::min(wall.Minimum, ao.At(x, y));
					++wall.Count;
				}
				SWIM_CHECK(ao.At(x, y) >= 0.0f && ao.At(x, y) <= 1.0f);
			}
		}
	}
	std::printf("             [gtao] crease %.3f (%u), 0.5 m %.3f (%u), open floor %.3f min %.3f (%u), open wall %.3f min %.3f (%u)\n",
		crease.Mean(), crease.Count, near.Mean(), near.Count, far.Mean(), double(far.Minimum), far.Count, wall.Mean(), double(wall.Minimum),
		wall.Count);
	SWIM_REQUIRE(crease.Count > 50u && near.Count > 50u && far.Count > 500u && wall.Count > 500u);
	SWIM_CHECK(far.Mean() > 0.97 && far.Minimum > 0.85f);							   // Flat and open: fully visible.
	SWIM_CHECK(wall.Mean() > 0.97 && wall.Minimum > 0.85f);							   // Also for a surface facing sideways.
	SWIM_CHECK(crease.Mean() < 0.8 && crease.Mean() > 0.4);							   // The wall hides a large share of the sky.
	SWIM_CHECK(crease.Mean() + 0.05 < near.Mean() && near.Mean() + 0.02 < far.Mean()); // Occlusion fades with distance.

	// Normal-less pixels (no opaque surface) are open.
	auto noNormals = inputs;
	for (auto& texel : noNormals.Normal.Texels)
	{
		texel = { 0, 0, 0, 0 };
	}
	const auto params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const auto open = Ss::Gtao(params, noNormals.Depth, noNormals.Normal);
	SWIM_CHECK(std::all_of(open.Texels.begin(), open.Texels.end(),
		[](float v)
		{
			return v == 1.0f;
		}));
}

SWIM_TEST("Render.ScreenSpace.Reference", "GtaoRadiusPowerAndSliceCounts")
{
	const auto scene = WallScene();
	const auto view = WallView();
	const auto inputs = Scene::Render(scene, view, Width, Height);
	ScreenSpaceSettings settings;
	settings.AmbientOcclusion.Radius = 1.0f;
	const auto base = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const auto raw = Ss::Gtao(base, inputs.Depth, inputs.Normal);
	const auto linear = Ss::Blur(base, raw, inputs.Depth);
	settings.AmbientOcclusion.Power = 2.0f;
	const auto squared = Ss::Blur(BuildScreenSpaceParams(settings, view, Width, Height, 0), raw, inputs.Depth);
	for (std::size_t i = 0; i < linear.Texels.size(); ++i)
	{
		SWIM_CHECK(Near(squared.Texels[i], linear.Texels[i] * linear.Texels[i], 1.0e-5f)); // Power applies after the blur.
	}

	// A larger radius reaches farther: the 0.5-1.5 m band gets darker.
	settings.AmbientOcclusion.Power = 1.0f;
	settings.AmbientOcclusion.Radius = 3.0f;
	const auto wide = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const auto wideAo = Ss::Blur(wide, Ss::Gtao(wide, inputs.Depth, inputs.Normal), inputs.Depth);
	SWIM_CHECK(FloorBand(inputs, wideAo, 0.7f, 1.5f).Mean() + 0.03 < FloorBand(inputs, linear, 0.7f, 1.5f).Mean());

	// One to four slices agree on average (the noise rotates the single slice).
	std::array<double, MaxAoSlices> means{};
	settings.AmbientOcclusion.Radius = 1.0f;
	for (std::uint32_t slices = 1; slices <= MaxAoSlices; ++slices)
	{
		settings.AmbientOcclusion.SliceCount = slices;
		const auto params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
		means[slices - 1] =
			FloorBand(inputs, Ss::Blur(params, Ss::Gtao(params, inputs.Depth, inputs.Normal), inputs.Depth), 0.0f, 1.0f).Mean();
	}
	for (std::uint32_t i = 1; i < MaxAoSlices; ++i)
	{
		SWIM_CHECK(std::abs(means[i] - means[0]) < 0.04);
	}

	// A radius that projects below one pixel is treated as open.
	settings.AmbientOcclusion.SliceCount = 2;
	settings.AmbientOcclusion.Radius = 0.001f;
	const auto tiny = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const auto tinyRaw = Ss::Gtao(tiny, inputs.Depth, inputs.Normal);
	SWIM_CHECK(std::all_of(tinyRaw.Texels.begin(), tinyRaw.Texels.end(),
		[](float v)
		{
			return v == 1.0f;
		}));
}

SWIM_TEST("Render.ScreenSpace.Reference", "BlurKeepsConstantsAndStopsAtDepthEdges")
{
	// A box on the floor: its top is far above the floor behind it on screen.
	Scene::Scene scene;
	scene.Boxes.push_back({ { -1.0f, 0.0f, -1.0f }, { 1.0f, 1.5f, 1.0f } });
	const auto view = Scene::View({ 0.0f, 4.0f, 6.0f }, { 0.0f, 0.5f, 0.0f }, float(Width) / float(Height));
	const auto inputs = Scene::Render(scene, view, Width, Height);
	const auto params = BuildScreenSpaceParams(ScreenSpaceSettings{}, view, Width, Height, 0);

	const Ss::ScalarImage constant(Width, Height, 0.6f);
	const auto blurred = Ss::Blur(params, constant, inputs.Depth);
	for (const float v : blurred.Texels)
	{
		SWIM_CHECK(Near(v, 0.6f, 1.0e-6f));
	}

	// AO 0 on the box, 1 elsewhere: floor pixels next to the box's silhouette stay 1.
	Ss::ScalarImage split(Width, Height, 1.0f);
	for (std::uint32_t i = 0; i < split.Texels.size(); ++i)
	{
		const auto& hit = inputs.Hits[i];
		split.Texels[i] = hit.T > 0.0f && hit.Position[1] > 0.01f ? 0.0f : 1.0f;
	}
	const auto kept = Ss::Blur(params, split, inputs.Depth);
	std::uint32_t edgeFloor = 0;
	for (std::uint32_t y = 2; y + 2 < Height; ++y)
	{
		for (std::uint32_t x = 2; x + 2 < Width; ++x)
		{
			const auto& hit = inputs.Hits[std::size_t(y) * Width + x];
			if (hit.T == 0.0f || hit.Position[1] > 0.01f || hit.Position[2] > -1.0f)
			{
				continue; // Only floor behind the box, where the depth jumps.
			}
			bool touchesBox = false;
			for (int dy = -2; dy <= 2; ++dy)
			{
				for (int dx = -2; dx <= 2; ++dx)
				{
					touchesBox = touchesBox || split.At(x + dx, y + dy) == 0.0f;
				}
			}
			if (touchesBox)
			{
				++edgeFloor;
				SWIM_CHECK(kept.At(x, y) > 0.95f);
			}
		}
	}
	SWIM_CHECK(edgeFloor > 20u);
	// Sky pixels keep their value (then clamp and power).
	Ss::ScalarImage sky(Width, Height, 1.5f);
	const auto clamped = Ss::Blur(params, sky, Ss::ScalarImage(Width, Height, 0.0f));
	SWIM_CHECK(std::all_of(clamped.Texels.begin(), clamped.Texels.end(),
		[](float v)
		{
			return v == 1.0f;
		}));
}

SWIM_TEST("Render.ScreenSpace.Reference", "CompositeOccludesOnlyIndirectLight")
{
	const auto view = WallView();
	ScreenSpaceSettings settings;
	const auto params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const Ss::Float4 color{ 2.0f, 1.0f, 0.5f, 0.75f };
	const Ss::Float4 indirect{ 0.8f, 0.4f, 0.2f, 1.0f };
	const auto half = Ss::CompositeTexel(params, color, indirect, 0.5f, 0.5f, 10, 10);
	SWIM_CHECK(Near(half[0], 1.6f, 1.0e-6f) && Near(half[1], 0.8f, 1.0e-6f) && Near(half[2], 0.4f, 1.0e-6f));
	SWIM_CHECK(half[3] == 0.75f); // Alpha is kept.
	const auto open = Ss::CompositeTexel(params, color, indirect, 1.0f, 0.5f, 10, 10);
	SWIM_CHECK(open == color);
	const auto closed = Ss::CompositeTexel(params, color, indirect, 0.0f, 0.5f, 10, 10);
	SWIM_CHECK(Near(closed[0], 1.2f, 1.0e-6f)); // Direct light and emission remain.
	const auto clampedLow = Ss::CompositeTexel(params, { 0.1f, 0.1f, 0.1f, 1.0f }, { 0.5f, 0.5f, 0.5f, 1.0f }, 0.0f, 0.5f, 10, 10);
	SWIM_CHECK(clampedLow[0] == 0.0f);
	settings.AmbientOcclusion.Enabled = false;
	const auto off = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	SWIM_CHECK(Ss::CompositeTexel(off, color, indirect, 0.0f, 0.5f, 10, 10) == color); // The AO input is ignored.
}

SWIM_TEST("Render.ScreenSpace.Reference", "FogMatchesItsIntegralsAndPhaseFunction")
{
	ScreenSpaceSettings settings;
	settings.Fog.Enabled = true;
	settings.Fog.Density = 0.05f;
	settings.Fog.HeightFalloff = 0.0f;
	const auto view = WallView();
	auto params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const Ss::Float3 camera{ 3.0f, 2.0f, 6.0f };
	// Uniform fog: tau = density * length.
	SWIM_CHECK(Near(Ss::FogOpticalDepth(params, camera, { 1, 0, 0 }, 20.0f), 1.0f, 1.0e-6f));
	SWIM_CHECK(Near(Ss::FogOpticalDepth(params, camera, { 0, 1, 0 }, 20.0f), 1.0f, 1.0e-5f));

	// Height fog against numeric integration, rays up, down and level.
	settings.Fog.HeightFalloff = 0.3f;
	settings.Fog.BaseHeight = 1.0f;
	settings.Fog.StartDistance = 2.0f;
	params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	for (const Ss::Float3 direction : { Ss::Float3{ 0.6f, 0.8f, 0.0f }, Ss::Float3{ 0.0f, -0.6f, 0.8f }, Ss::Float3{ 1.0f, 0.0f, 0.0f },
			 Ss::Float3{ 0.0f, 1.0e-6f, 1.0f } })
	{
		const float distance = 15.0f;
		double numeric = 0.0;
		constexpr int steps = 20000;
		const double length = distance - settings.Fog.StartDistance;
		for (int i = 0; i < steps; ++i)
		{
			const double t = settings.Fog.StartDistance + (i + 0.5) * length / steps;
			const double height = camera[1] + direction[1] * t;
			numeric += settings.Fog.Density * std::exp(-settings.Fog.HeightFalloff * (height - settings.Fog.BaseHeight)) * length / steps;
		}
		const float analytic = Ss::FogOpticalDepth(params, camera, direction, distance);
		SWIM_CHECK(std::abs(analytic - numeric) <= 1.0e-4 * numeric + 1.0e-7);
	}
	SWIM_CHECK(Ss::FogOpticalDepth(params, camera, { 1, 0, 0 }, 1.5f) == 0.0f); // Before the start distance.

	// Henyey-Greenstein: 1 when isotropic, integrates to 4 pi, peaks forward for g > 0.
	SWIM_CHECK(Near(Ss::HenyeyGreenstein(0.0f, 0.3f), 1.0f, 1.0e-6f));
	for (const float g : { -0.5f, 0.3f, 0.8f })
	{
		double integral = 0.0;
		constexpr int steps = 4000;
		for (int i = 0; i < steps; ++i)
		{
			const double mu = -1.0 + (i + 0.5) * 2.0 / steps;
			integral += Ss::HenyeyGreenstein(g, float(mu)) * 2.0 / steps * 2.0 * std::numbers::pi;
		}
		SWIM_CHECK(std::abs(integral - 4.0 * std::numbers::pi) < 1.0e-3 * 4.0 * std::numbers::pi);
	}
	SWIM_CHECK(Ss::HenyeyGreenstein(0.6f, 1.0f) > Ss::HenyeyGreenstein(0.6f, -1.0f));

	// Composite: surfaces fade toward the in-scattered color with their distance; the sky
	// uses the max distance; looking toward the sun adds its lobe.
	const auto scene = WallScene();
	const auto inputs = Scene::Render(scene, view, Width, Height);
	settings.AmbientOcclusion.Enabled = false;
	settings.Fog.StartDistance = 0.0f;
	settings.Fog.SunColor = { 2.0f, 1.5f, 1.0f };
	settings.Fog.SunDirection = { -1.0f, 0.0f, 0.0f }; // Sunlight travels toward -x.
	settings.Fog.MaxDistance = 30.0f;
	params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const Ss::Float4 color{ 1.0f, 0.5f, 0.25f, 1.0f };
	std::uint32_t checked = 0;
	for (std::uint32_t y = 0; y < Height; y += 7)
	{
		for (std::uint32_t x = 0; x < Width; x += 7)
		{
			const auto& hit = inputs.Hits[std::size_t(y) * Width + x];
			const auto result = Ss::CompositeTexel(params, color, { 0, 0, 0, 0 }, 1.0f, inputs.Depth.At(x, y), x, y);
			Ss::Float3 direction{};
			float distance = 0.0f;
			if (hit.T > 0.0f)
			{
				direction = { (hit.Position[0] - camera[0]) / hit.T, (hit.Position[1] - camera[1]) / hit.T,
					(hit.Position[2] - camera[2]) / hit.T };
				distance = std::min(hit.T, 30.0f); // Capped at the max distance.
			}
			else
			{
				continue;
			}
			const auto fog = Ss::EvaluateFog(params, camera, direction, distance);
			for (int c = 0; c < 3; ++c)
			{
				const float expected = color[c] * fog.Transmittance + fog.Inscatter[c] * (1.0f - fog.Transmittance);
				SWIM_CHECK(Near(result[c], expected, 2.0e-3f * (1.0f + expected)));
			}
			SWIM_CHECK(result[3] == 1.0f);
			++checked;
		}
	}
	SWIM_CHECK(checked > 100u);
	// Sky: the max distance.
	auto skyInputs = Scene::Render(Scene::Scene{ false, 0.0f, {} }, view, Width, Height);
	const auto sky = Ss::CompositeTexel(params, color, { 0, 0, 0, 0 }, 1.0f, skyInputs.Depth.At(80, 5), 80, 5);
	const auto skyDirection = [&]
	{
		const auto p = *Ss::ViewPosition(params, 80.5f, 5.5f, 1.0f);
		Ss::Float3 world{};
		for (int r = 0; r < 3; ++r)
		{
			world[r] = params.InverseViewRows[r * 4] * p[0] + params.InverseViewRows[r * 4 + 1] * p[1] +
				params.InverseViewRows[r * 4 + 2] * p[2] + params.InverseViewRows[r * 4 + 3] - camera[r];
		}
		const float l = std::sqrt(world[0] * world[0] + world[1] * world[1] + world[2] * world[2]);
		return Ss::Float3{ world[0] / l, world[1] / l, world[2] / l };
	}();
	const auto skyFog = Ss::EvaluateFog(params, camera, skyDirection, 30.0f);
	SWIM_CHECK(Near(sky[0], color[0] * skyFog.Transmittance + skyFog.Inscatter[0] * (1.0f - skyFog.Transmittance), 1.0e-3f));
	// Looking toward the sun (+x, against the light's travel) the in-scattering is brighter than away from it.
	const auto toward = Ss::EvaluateFog(params, camera, { 1, 0, 0 }, 10.0f);
	const auto away = Ss::EvaluateFog(params, camera, { -1, 0, 0 }, 10.0f);
	SWIM_CHECK(toward.Inscatter[0] > away.Inscatter[0] + 1.0f);
	// Zero density: the identity.
	settings.Fog.Density = 0.0f;
	const auto clear = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	SWIM_CHECK(Ss::CompositeTexel(clear, color, { 0, 0, 0, 0 }, 1.0f, inputs.Depth.At(80, 60), 80, 60) == color);
}

namespace
{
	// A mirror floor in front of a box: the floor between the camera and the box
	// reflects the box's front face, the rest reflects the sky.
	Scene::Scene MirrorScene()
	{
		Scene::Scene scene;
		scene.Boxes.push_back({ { -1.0f, 0.0f, -1.0f }, { 1.0f, 2.0f, 1.0f } });
		return scene;
	}

	ScreenSpaceView MirrorView()
	{
		return Scene::View({ 0.4f, 1.5f, 6.0f }, { 0.0f, 0.8f, 0.0f }, float(Width) / float(Height));
	}

	ScreenSpaceSettings MirrorSettings()
	{
		ScreenSpaceSettings settings;
		settings.AmbientOcclusion.Enabled = false;
		settings.Reflections.Enabled = true;
		settings.Reflections.Stride = 1.0f;
		settings.Reflections.MaxSteps = 256;
		settings.Reflections.RefineSteps = 6;
		settings.Reflections.Thickness = 0.2f;
		settings.Reflections.EdgeFade = 0.02f;
		return settings;
	}

	Ss::Float3 Reflect(const Ss::Float3& d, const Ss::Float3& n)
	{
		const float k = 2.0f * (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]);
		return { d[0] - k * n[0], d[1] - k * n[1], d[2] - k * n[2] };
	}

	float Distance(const Ss::Float3& a, const Ss::Float3& b)
	{
		return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
	}
} // namespace

SWIM_TEST("Render.ScreenSpace.Reference", "ReflectionsFindTheMirrorImageAndMissTheSky")
{
	const auto scene = MirrorScene();
	const auto view = MirrorView();
	const auto inputs = Scene::Render(scene, view, Width, Height, 0.0f);
	const auto params = BuildScreenSpaceParams(MirrorSettings(), view, Width, Height, 0);
	const auto viewProjection = MultiplyRowMajor(view.Projection, view.View);

	std::uint32_t visible = 0, found = 0, accurate = 0, sky = 0, skyMisses = 0, distances = 0;
	float worst = 0.0f;
	for (std::uint32_t y = 0; y < Height; ++y)
	{
		for (std::uint32_t x = 0; x < Width; ++x)
		{
			const auto& hit = inputs.Hits[std::size_t(y) * Width + x];
			if (hit.T == 0.0f || hit.Normal[1] < 0.5f || hit.Position[1] > 1.0e-3f)
			{
				continue; // Floor pixels only.
			}
			const Ss::Float3 incoming{ (hit.Position[0] - inputs.Camera[0]) / hit.T, (hit.Position[1] - inputs.Camera[1]) / hit.T,
				(hit.Position[2] - inputs.Camera[2]) / hit.T };
			const auto truth = Scene::Cast(scene, hit.Position, Reflect(incoming, hit.Normal));
			const auto traced = Ss::TraceReflection(params, inputs.Depth, inputs.Normal, x, y);
			if (!truth)
			{
				++sky;
				skyMisses += traced ? 0u : 1u;
				continue;
			}
			// Is the true reflected point on screen and seen directly by the camera?
			std::array<float, 4> clip{};
			for (int r = 0; r < 4; ++r)
			{
				clip[r] = viewProjection[r * 4] * truth->Position[0] + viewProjection[r * 4 + 1] * truth->Position[1] +
					viewProjection[r * 4 + 2] * truth->Position[2] + viewProjection[r * 4 + 3];
			}
			const float px = (clip[0] / clip[3] + 1.0f) * 0.5f * float(Width);
			const float py = (1.0f - clip[1] / clip[3]) * 0.5f * float(Height);
			// Well inside the screen (the edge fade is tiny) and seen directly.
			if (!(px >= 4.0f && py >= 4.0f && px < float(Width) - 4.0f && py < float(Height) - 4.0f))
			{
				continue;
			}
			const auto& direct = inputs.Hits[std::size_t(py) * Width + std::size_t(px)];
			if (direct.T == 0.0f || Distance(direct.Position, truth->Position) > 0.05f)
			{
				continue;
			}
			++visible;
			if (!traced)
			{
				continue;
			}
			++found;
			const auto& at = inputs.Hits[std::size_t(traced->Y) * Width + traced->X];
			const float error = Distance(at.Position, truth->Position);
			worst = std::max(worst, error);
			accurate += error < 0.1f ? 1u : 0u;
			SWIM_CHECK(traced->Confidence > 0.0f && traced->Confidence <= 1.0f);
			distances += std::abs(traced->Distance - truth->T) < 0.15f ? 1u : 0u; // The travelled distance.
		}
	}
	std::printf("             [ssr] %u visible reflections: %u found, %u within 0.1 m (worst %.3f m); %u sky rays, %u missed\n", visible,
		found, accurate, double(worst), sky, skyMisses);
	SWIM_REQUIRE(visible > 200u && sky > 200u);
	SWIM_CHECK(found >= visible * 95 / 100);
	SWIM_CHECK(accurate >= found * 95 / 100);
	SWIM_CHECK(distances >= found * 95 / 100);
	SWIM_CHECK(skyMisses >= sky * 97 / 100);
}

SWIM_TEST("Render.ScreenSpace.Reference", "ReflectionConfidenceFadesWithRoughnessEdgesAndDistance")
{
	const auto scene = MirrorScene();
	const auto view = MirrorView();
	const auto settings = MirrorSettings();
	const auto base = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const auto mirror = Scene::Render(scene, view, Width, Height, 0.0f);

	// Pixels whose mirror ray hits with full confidence.
	std::vector<std::pair<std::uint32_t, std::uint32_t>> strong;
	for (std::uint32_t y = 0; y < Height; ++y)
	{
		for (std::uint32_t x = 0; x < Width; ++x)
		{
			const auto hit = Ss::TraceReflection(base, mirror.Depth, mirror.Normal, x, y);
			if (hit && hit->Confidence == 1.0f)
			{
				strong.push_back({ x, y });
			}
		}
	}
	SWIM_REQUIRE(strong.size() > 100u);

	// Roughness: half way down the fade halves the confidence; at the limit nothing is traced.
	const float halfway = settings.Reflections.MaxRoughness - 0.5f * settings.Reflections.RoughnessFade;
	const auto glossy = Scene::Render(scene, view, Width, Height, halfway);
	const auto rough = Scene::Render(scene, view, Width, Height, settings.Reflections.MaxRoughness);
	for (const auto& [x, y] : strong)
	{
		const auto hit = Ss::TraceReflection(base, glossy.Depth, glossy.Normal, x, y);
		SWIM_REQUIRE(hit.has_value());
		SWIM_CHECK(Near(hit->Confidence, 0.5f, 1.0e-5f));
		SWIM_CHECK(!Ss::TraceReflection(base, rough.Depth, rough.Normal, x, y));
	}

	// Distance: nothing beyond MaxDistance is hit, and the confidence falls linearly over it.
	auto shortSettings = settings;
	shortSettings.Reflections.MaxDistance = 1.0f;
	shortSettings.Reflections.DistanceFade = 1.0f;
	const auto shortParams = BuildScreenSpaceParams(shortSettings, view, Width, Height, 0);
	std::uint32_t faded = 0;
	for (const auto& [x, y] : strong)
	{
		const auto limited = Ss::TraceReflection(shortParams, mirror.Depth, mirror.Normal, x, y);
		if (limited)
		{
			SWIM_CHECK(limited->Distance <= 1.0f + 1.0e-4f);
			SWIM_CHECK(Near(limited->Confidence, std::clamp(1.0f - limited->Distance, 0.0f, 1.0f), 1.0e-4f));
			++faded;
		}
	}
	SWIM_CHECK(faded > 0u);

	// Edges: a wide edge fade lowers the confidence of hits near the border and never raises it.
	auto edgeSettings = settings;
	edgeSettings.Reflections.EdgeFade = 0.5f;
	const auto edgeParams = BuildScreenSpaceParams(edgeSettings, view, Width, Height, 0);
	for (const auto& [x, y] : strong)
	{
		const auto hit = Ss::TraceReflection(edgeParams, mirror.Depth, mirror.Normal, x, y);
		SWIM_REQUIRE(hit.has_value());
		SWIM_CHECK(hit->X == static_cast<std::uint32_t>(std::floor(hit->HitX)) && hit->Y == static_cast<std::uint32_t>(std::floor(hit->HitY)));
		const float u = hit->HitX / float(Width);
		const float v = hit->HitY / float(Height);
		const float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
		SWIM_CHECK(Near(hit->Confidence, std::clamp(edge / 0.5f, 0.0f, 1.0f), 1.0e-5f));
	}
}

SWIM_TEST("Render.ScreenSpace.Reference", "ReflectionsRejectBackFacesSkyAndClipToTheNearPlane")
{
	const auto scene = MirrorScene();
	const auto view = MirrorView();
	const auto params = BuildScreenSpaceParams(MirrorSettings(), view, Width, Height, 0);
	auto inputs = Scene::Render(scene, view, Width, Height, 0.0f);
	SWIM_CHECK(Near(params.SsrNearZ, -0.1f, 1.0e-6f));

	// Sky pixels and normal-less pixels trace nothing.
	std::uint32_t tested = 0;
	for (std::uint32_t y = 0; y < Height; ++y)
	{
		for (std::uint32_t x = 0; x < Width; ++x)
		{
			if (inputs.Depth.At(x, y) == 0.0f)
			{
				SWIM_CHECK(!Ss::TraceReflection(params, inputs.Depth, inputs.Normal, x, y));
				++tested;
			}
		}
	}
	SWIM_CHECK(tested > 0u);

	// Flip the box's normals: every hit on it becomes a back face and is rejected.
	std::vector<std::pair<std::uint32_t, std::uint32_t>> hits;
	for (std::uint32_t y = 0; y < Height; ++y)
	{
		for (std::uint32_t x = 0; x < Width; ++x)
		{
			if (Ss::TraceReflection(params, inputs.Depth, inputs.Normal, x, y))
			{
				hits.push_back({ x, y });
			}
		}
	}
	SWIM_REQUIRE(!hits.empty());
	auto flipped = inputs.Normal;
	for (auto& texel : flipped.Texels)
	{
		if (texel[1] < 0.5f)
		{
			texel = { -texel[0], -texel[1], -texel[2], texel[3] };
		}
	}
	for (const auto& [x, y] : hits)
	{
		const auto hit = Ss::TraceReflection(params, inputs.Depth, inputs.Normal, x, y);
		const auto& hitNormal = inputs.Normal.At(hit->X, hit->Y);
		if (hitNormal[1] < 0.5f) // The hit is on the box.
		{
			const auto again = Ss::TraceReflection(params, inputs.Depth, flipped, x, y);
			SWIM_CHECK(!again || flipped.At(again->X, again->Y)[1] >= 0.5f);
		}
	}

	// A mirror facing the camera sends rays back toward it: they are clipped to the near
	// plane and stay finite.
	Scene::Scene wall;
	wall.Ground = false;
	wall.Boxes.push_back({ { -10.0f, -10.0f, -1.0f }, { 10.0f, 10.0f, 0.0f } });
	const auto wallView = Scene::View({ 0.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, float(Width) / float(Height));
	const auto wallInputs = Scene::Render(wall, wallView, Width, Height, 0.0f);
	const auto wallParams = BuildScreenSpaceParams(MirrorSettings(), wallView, Width, Height, 0);
	for (std::uint32_t y = 0; y < Height; y += 7)
	{
		for (std::uint32_t x = 0; x < Width; x += 7)
		{
			const auto hit = Ss::TraceReflection(wallParams, wallInputs.Depth, wallInputs.Normal, x, y);
			SWIM_CHECK(!hit || (std::isfinite(hit->Distance) && hit->Distance <= 3.0f)); // Never past the near plane.
		}
	}

	// Reflections need a near plane in front of the camera.
	auto bad = view;
	bad.Projection[11] = -bad.Projection[11];
	SWIM_CHECK_THROWS(BuildScreenSpaceParams(MirrorSettings(), bad, Width, Height, 0), std::invalid_argument);
	ScreenSpaceSettings invalid = MirrorSettings();
	invalid.Reflections.Stride = 0.5f;
	SWIM_CHECK_THROWS(BuildScreenSpaceParams(invalid, view, Width, Height, 0), std::invalid_argument);
	invalid = MirrorSettings();
	invalid.Reflections.EdgeFade = 0.0f;
	SWIM_CHECK_THROWS(BuildScreenSpaceParams(invalid, view, Width, Height, 0), std::invalid_argument);
	invalid = MirrorSettings();
	invalid.Reflections.Thickness = -1.0f;
	SWIM_CHECK_THROWS(BuildScreenSpaceParams(invalid, view, Width, Height, 0), std::invalid_argument);
	invalid = MirrorSettings();
	invalid.Reflections.RefineSteps = MaxReflectionRefineSteps + 1;
	SWIM_CHECK_THROWS(BuildScreenSpaceParams(invalid, view, Width, Height, 0), std::invalid_argument);
}

SWIM_TEST("Render.ScreenSpace.Reference", "CompositeReplacesTheSpecularIblWithTheReflection")
{
	auto view = MirrorView();
	ScreenSpaceSettings settings;
	settings.Reflections.Enabled = true;
	const auto params = BuildScreenSpaceParams(settings, view, Width, Height, 0);
	const Ss::Float4 color{ 2.0f, 1.5f, 1.0f, 1.0f };
	const Ss::Float4 indirect{ 0.8f, 0.6f, 0.4f, 1.0f };
	Ss::ReflectionSample reflection;
	reflection.Reflection = { 3.0f, 2.0f, 1.0f, 0.75f };
	reflection.Reflectance = { 0.5f, 0.4f, 0.3f, 1.0f };
	reflection.Specular = { 0.2f, 0.15f, 0.1f, 1.0f };
	const float ao = 0.6f;
	const auto out = Ss::CompositeTexel(params, color, indirect, ao, reflection, 0.5f, 10, 10);
	for (int c = 0; c < 3; ++c)
	{
		const float afterAo = color[c] - (1.0f - ao) * indirect[c];
		const float expected = afterAo + 0.75f * ao * (reflection.Reflectance[c] * reflection.Reflection[c] - reflection.Specular[c]);
		SWIM_CHECK(Near(out[c], expected, 1.0e-6f));
	}
	SWIM_CHECK(out[3] == 1.0f);
	// A miss (confidence 0) leaves the color as AO left it; reflections off ignore the inputs.
	auto miss = reflection;
	miss.Reflection[3] = 0.0f;
	const auto kept = Ss::CompositeTexel(params, color, indirect, ao, miss, 0.5f, 10, 10);
	auto off = params;
	off.SsrEnabled = 0;
	const auto ignored = Ss::CompositeTexel(off, color, indirect, ao, reflection, 0.5f, 10, 10);
	for (int c = 0; c < 3; ++c)
	{
		SWIM_CHECK(Near(kept[c], color[c] - (1.0f - ao) * indirect[c], 1.0e-6f));
		SWIM_CHECK(kept[c] == ignored[c]);
	}
	// With AO off the full specular IBL is replaced; a darker reflection never goes negative.
	auto noAo = params;
	noAo.AoEnabled = 0;
	auto dark = reflection;
	dark.Reflection = { 0.0f, 0.0f, 0.0f, 1.0f };
	dark.Specular = { 5.0f, 5.0f, 5.0f, 1.0f };
	const auto clamped = Ss::CompositeTexel(noAo, color, indirect, 0.2f, dark, 0.5f, 10, 10);
	SWIM_CHECK(clamped[0] == 0.0f && clamped[1] == 0.0f && clamped[2] == 0.0f);

	// The hit radiance carries the hit's own occlusion.
	Ss::ColorImage colors(2, 1, { 1.0f, 1.0f, 1.0f, 1.0f });
	Ss::ColorImage indirects(2, 1, { 0.5f, 0.25f, 0.0f, 1.0f });
	Ss::ScalarImage occlusion(2, 1, 0.5f);
	const auto radiance = Ss::HitRadiance(params, colors, indirects, &occlusion, 1, 0);
	SWIM_CHECK(Near(radiance[0], 0.75f, 1.0e-6f) && Near(radiance[1], 0.875f, 1.0e-6f) && radiance[2] == 1.0f);
	auto noOcclusion = params;
	noOcclusion.AoEnabled = 0;
	SWIM_CHECK(Ss::HitRadiance(noOcclusion, colors, indirects, &occlusion, 1, 0)[0] == 1.0f);

	// The filtered radiance: a texel centre returns that texel, a uniform area itself, and
	// a lone highlight is damped by its luminance instead of flickering at full strength.
	Ss::ColorImage patch(4, 4, { 0.5f, 0.5f, 0.5f, 1.0f });
	const auto uniform = Ss::FilteredHitRadiance(noOcclusion, patch, patch, nullptr, 2.3f, 1.7f);
	SWIM_CHECK(Near(uniform[0], 0.5f, 1.0e-6f) && Near(uniform[2], 0.5f, 1.0e-6f));
	patch.At(2, 1) = { 50.0f, 50.0f, 50.0f, 1.0f };
	const auto centre = Ss::FilteredHitRadiance(noOcclusion, patch, patch, nullptr, 2.5f, 1.5f);
	SWIM_CHECK(Near(centre[1], 50.0f, 1.0e-4f));
	const auto between = Ss::FilteredHitRadiance(noOcclusion, patch, patch, nullptr, 2.0f, 1.0f); // A quarter of the highlight.
	SWIM_CHECK(between[0] > 0.5f && between[0] < 0.25f * 50.0f + 0.75f * 0.5f);
	SWIM_CHECK(between[0] < 2.0f);
	const auto corner = Ss::FilteredHitRadiance(noOcclusion, patch, patch, nullptr, -3.0f, 9.0f); // Clamped to the image.
	SWIM_CHECK(Near(corner[0], 0.5f, 1.0e-6f));
}

SWIM_TEST("Render.ScreenSpace.Reference", "ReflectionsDoNotAliasToTheStride")
{
	// With a coarse stride many samples land far behind the box's faces (more than the
	// thickness): the crossing test still finds those hits and refines them to the same
	// place as a one-pixel stride, so reflected edges do not alias to the stride.
	const auto scene = MirrorScene();
	const auto view = MirrorView();
	const auto inputs = Scene::Render(scene, view, Width, Height, 0.0f);
	auto fine = MirrorSettings();
	auto coarse = fine;
	coarse.Reflections.Stride = 4.0f;
	coarse.Reflections.MaxSteps = 64;
	const auto fineParams = BuildScreenSpaceParams(fine, view, Width, Height, 0);
	const auto coarseParams = BuildScreenSpaceParams(coarse, view, Width, Height, 0);
	std::uint32_t fineHits = 0, coarseHits = 0, agree = 0;
	for (std::uint32_t y = 0; y < Height; ++y)
	{
		for (std::uint32_t x = 0; x < Width; ++x)
		{
			const auto a = Ss::TraceReflection(fineParams, inputs.Depth, inputs.Normal, x, y);
			const auto b = Ss::TraceReflection(coarseParams, inputs.Depth, inputs.Normal, x, y);
			fineHits += a ? 1u : 0u;
			coarseHits += b ? 1u : 0u;
			if (a && b)
			{
				agree += std::abs(a->HitX - b->HitX) <= 1.5f && std::abs(a->HitY - b->HitY) <= 1.5f ? 1u : 0u;
			}
		}
	}
	std::printf("             [ssr] stride 1: %u hits, stride 4: %u hits, %u within 1.5 px\n", fineHits, coarseHits, agree);
	SWIM_REQUIRE(fineHits > 200u);
	// At 160 x 90 a 4-pixel stride still steps over whole faces near the ray's start
	// (sample-based marching cannot see those); the crossing test keeps about three
	// quarters of the hits, where the thickness test alone kept 42 %.
	SWIM_CHECK(coarseHits >= fineHits * 70 / 100);
	SWIM_CHECK(agree >= coarseHits * 97 / 100);
}
