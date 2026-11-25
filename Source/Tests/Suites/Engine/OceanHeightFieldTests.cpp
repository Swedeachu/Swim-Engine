#include "Engine/Systems/Renderer/Features/Ocean.h"
#include "Engine/Systems/Water/OceanHeightField.h"
#include "Tests/Framework/Test.h"

#include <cmath>

using namespace Engine;

namespace
{

	constexpr float TwoPi = 6.28318530718f;

	// A choppy wave along +x that tiles every L metres: rest u moves to u + Choppy cos(k u - w t)
	// and up by Amplitude sin(k u - w t).
	struct TestWave
	{
		float L = 64.0f;
		float K = TwoPi * 2.0f / 64.0f;
		float W = 2.0f;
		float Amplitude = 1.0f;
		float Choppy = 0.6f;

		glm::vec3 At(float u, float t) const
		{
			const float theta = K * u - W * t;
			return { Choppy * std::cos(theta), Amplitude * std::sin(theta), 0.0f };
		}

		OceanFieldCascade Field(std::uint32_t n, float t) const
		{
			OceanFieldCascade c;
			c.TileSize = L;
			c.Resolution = n;
			c.Displacement.resize(std::size_t(n) * n);

			for (std::uint32_t y = 0; y < n; ++y)
			{
				for (std::uint32_t x = 0; x < n; ++x)
				{
					c.Displacement[std::size_t(y) * n + x] = At((x + 0.5f) * L / n, t);
				}
			}

			return c;
		}

		// The rest point whose displaced x is closest to x (a brute-force search).
		float RestAt(float x, float t) const
		{
			float best = 1.0e9f, rest = x;

			for (float u = x - 2.0f; u <= x + 2.0f; u += 0.0005f)
			{
				const float error = std::abs(u + At(u, t).x - x);

				if (error < best)
				{
					best = error;
					rest = u;
				}
			}

			return rest;
		}

		float HeightAt(float x, float t) const { return At(RestAt(x, t), t).y; }
	};

} // namespace

SWIM_TEST("Engine.OceanHeightField", "FlatUntilPublishedThenOnTheDisplacedSurface")
{
	const TestWave wave;
	OceanHeightField field;
	const auto flat = field.Sample(3.0f, 4.0f, 1.5f, 0.0);
	SWIM_CHECK(!field.HasData() && flat.SurfaceHeight == 1.5f && flat.FlowVelocity == glm::vec3(0.0f));

	field.Publish({ wave.Field(256, 0.0f) }, 0.0, 256.0);
	SWIM_REQUIRE(field.HasData());

	// Far from the origin too: the tiles repeat.
	for (const float x : { 0.3f, 5.0f, 11.7f, 23.2f, 40.0f, 63.9f, -17.5f, 1000.25f })
	{
		const auto sample = field.Sample(x, 9.0f, 1.5f, 0.0);
		SWIM_CHECK(std::abs(sample.SurfaceHeight - (1.5f + wave.HeightAt(x, 0.0f))) < 0.02f);
	}
}

SWIM_TEST("Engine.OceanHeightField", "VelocityFromTwoFieldsAndLateDataCatchesUp")
{
	const TestWave wave;
	OceanHeightField field;
	field.Publish({ wave.Field(256, 1.0f) }, 1.0, 256.0);
	field.Publish({ wave.Field(256, 1.05f) }, 1.05, 256.0);

	const float x = 7.0f;
	const auto sample = field.Sample(x, 0.0f, 0.0f, 1.05);
	// The surface point's own vertical speed (at its rest point).
	const float rest = wave.RestAt(x, 1.05f);
	const float expected = (wave.At(rest, 1.05f).y - wave.At(rest, 1.0f).y) / 0.05f;
	SWIM_CHECK(std::abs(sample.SurfaceHeight - wave.HeightAt(x, 1.05f)) < 0.02f);
	SWIM_CHECK(std::abs(sample.FlowVelocity.y - expected) < 0.25f);

	// Read 0.05 s after the newest field: moved along the velocity toward the true height.
	const auto later = field.Sample(x, 0.0f, 0.0f, 1.1);
	SWIM_CHECK(std::abs(later.SurfaceHeight - wave.HeightAt(x, 1.1f)) < std::abs(sample.SurfaceHeight - wave.HeightAt(x, 1.1f)));

	// Across the clock's wrap (255.95 -> 0.0 is 0.05 s).
	OceanHeightField wrapped;
	wrapped.Publish({ wave.Field(256, 1.0f) }, 255.95, 256.0);
	wrapped.Publish({ wave.Field(256, 1.05f) }, 0.0, 256.0);
	SWIM_CHECK(std::abs(wrapped.Sample(x, 0.0f, 0.0f, 0.0).FlowVelocity.y - expected) < 0.25f);
}

SWIM_TEST("Engine.OceanHeightField", "HalfFloatTexelsDecimateOntoTexelCentres")
{
	// 4 x 4 RGBA16F: x displacement = column (0, 0.5, 1, 2 as halves), y = -2 everywhere.
	const std::uint16_t columns[4] = { 0x0000, 0x3800, 0x3C00, 0x4000 };
	std::vector<std::uint16_t> texels;

	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			texels.insert(texels.end(), { columns[x], 0xC000, 0x0000, 0x0000 });
		}
	}

	const auto full = OceanFieldCascade::FromHalfFloats(texels, 4, 1, 8.0f);
	SWIM_CHECK(full.Resolution == 4 && full.Displacement[2] == glm::vec3(1.0f, -2.0f, 0.0f));
	const auto half = OceanFieldCascade::FromHalfFloats(texels, 4, 2, 8.0f);
	SWIM_REQUIRE(half.Resolution == 2);
	SWIM_CHECK(half.Displacement[0] == glm::vec3(0.25f, -2.0f, 0.0f));
	SWIM_CHECK(half.Displacement[1] == glm::vec3(1.5f, -2.0f, 0.0f));
}

SWIM_TEST("Engine.OceanHeightField", "OceanSurfaceFunctionIsFlatBeforeTheFirstReadback")
{
	auto simulation = std::make_shared<OceanSimulation>();
	const auto surface = MakeOceanFluidSurface(simulation, 4.0f);
	const auto sample = surface(10.0f, -3.0f, 0.0);
	SWIM_CHECK(sample.SurfaceHeight == 4.0f && sample.FlowVelocity == glm::vec3(0.0f));
	SWIM_CHECK(!simulation->HasSurfaceData());
}
