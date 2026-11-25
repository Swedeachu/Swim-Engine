#include "Engine/Systems/Water/WaterWaves.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <stdexcept>

using namespace Engine;

SWIM_TEST("Engine.WaterWaves", "LodFadesContinuouslyAndValidates")
{
	WaterLodSettings lod;
	const auto near = EvaluateWaterLod(lod, 0.0f);
	const auto far = EvaluateWaterLod(lod, 1.0e5f);
	SWIM_CHECK(near.Detail == 1.0f && near.Amplitude == 1.0f && near.Normal == 1.0f && near.Reflection == 1.0f);
	SWIM_CHECK(far.Detail == 0.0f && std::abs(far.Amplitude - lod.FarAmplitude) < 1.0e-6f);

	// No step anywhere: neighbouring distances differ by little.
	float previous = EvaluateWaterLod(lod, 0.0f).Amplitude;

	for (float d = 1.0f; d < 1000.0f; d += 1.0f)
	{
		const float amplitude = EvaluateWaterLod(lod, d).Amplitude;
		SWIM_CHECK(amplitude <= previous + 1.0e-6f && previous - amplitude < 0.01f);
		previous = amplitude;
	}

	lod.DetailEnd = lod.DetailStart - 1.0f;
	bool threw = false;

	try
	{
		ValidateWaterLodSettings(lod);
	}
	catch (const std::invalid_argument&)
	{
		threw = true;
	}

	SWIM_CHECK(threw);
}

SWIM_TEST("Engine.WaterWaves", "SurfaceSampleLandsOnTheDisplacedSurface")
{
	const auto set = BuildWaterWaves(WaterWaveSettings{ .Height = 2.0f });
	SWIM_REQUIRE(set.Count > 0);

	for (float x = -20.0f; x <= 20.0f; x += 7.3f)
	{
		const auto sample = SampleWaterSurface(set, x, 3.0f, 12.5);
		// The rest point under (x, 3) displaces back onto (x, 3) at the sampled height.
		bool found = false;

		for (float rx = x - 3.0f; rx <= x + 3.0f && !found; rx += 0.01f)
		{
			for (float rz = 0.0f; rz <= 6.0f && !found; rz += 0.02f)
			{
				const glm::vec3 p = EvaluateWaterWaves(set, { rx, rz }, 12.5);
				found = std::abs(p.x - x) < 0.02f && std::abs(p.z - 3.0f) < 0.05f && std::abs(p.y - sample.Height) < 0.05f;
			}
		}

		SWIM_CHECK(found);
		SWIM_CHECK(std::abs(sample.Height - 2.0f) < 2.0f && sample.Normal.y > 0.5f);
	}
}
