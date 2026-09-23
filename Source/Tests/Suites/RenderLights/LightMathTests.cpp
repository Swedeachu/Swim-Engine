#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <random>

using namespace Swim;
using namespace Swim::Render;
namespace L = Swim::Render::Lights;
namespace Pbr = Swim::Render::StandardPbr;

namespace
{
	LightDesc Spot(float inner, float outer, float range = 10.0f)
	{
		LightDesc desc;
		desc.Type = LightType::Spot;
		desc.Position = { 1, 2, 3 };
		desc.Direction = { 0, 0, -2 }; // Normalized on encode.
		desc.Range = range;
		desc.InnerConeAngle = inner;
		desc.OuterConeAngle = outer;
		return desc;
	}

	float Length(const L::Float3& v)
	{
		return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}
} // namespace

SWIM_TEST("Render.Lights", "EncodingValidatesDescsAndPacksTheGpuRecord")
{
	LightDesc point;
	point.Position = { 1, 2, 3 };
	point.Color = { 1.0f, 0.5f, 0.25f };
	point.Intensity = 8.0f;
	point.Range = 4.0f;
	point.Flags = LightFlags::CastsShadows;
	point.ShadowIndex = 7;
	const auto record = L::EncodeLight(point);
	SWIM_CHECK_EQUAL(record.Type, 1u);
	SWIM_CHECK(record.Position[0] == 1 && record.Position[2] == 3);
	SWIM_CHECK(record.Color[0] == 8.0f && record.Color[1] == 4.0f && record.Color[2] == 2.0f); // Color * intensity.
	SWIM_CHECK_EQUAL(record.Range, 4.0f);
	SWIM_CHECK_EQUAL(record.InverseRangeSquared, 1.0f / 16.0f);
	SWIM_CHECK(record.SpotScale == 0.0f && record.SpotOffset == 1.0f); // No angular falloff.
	SWIM_CHECK_EQUAL(record.ShadowIndex, 7u);
	SWIM_CHECK_EQUAL(record.Flags, 1u);

	LightDesc sun;
	sun.Type = LightType::Directional;
	sun.Direction = { 0, -3, 4 };
	sun.Intensity = 2.0f;
	const auto directional = L::EncodeLight(sun);
	SWIM_CHECK_EQUAL(directional.Type, 0u);
	SWIM_CHECK(std::abs(directional.Direction[1] + 0.6f) < 1.0e-6f && std::abs(directional.Direction[2] - 0.8f) < 1.0e-6f);
	SWIM_CHECK(directional.Range == 0.0f && directional.InverseRangeSquared == 0.0f);
	SWIM_CHECK_EQUAL(directional.ShadowIndex, GpuLightNoShadow);

	// glTF spot constants: scale = 1 / (cos inner - cos outer), offset = -cos outer * scale.
	const auto spot = L::EncodeLight(Spot(0.2f, 0.5f));
	const float scale = 1.0f / (std::cos(0.2f) - std::cos(0.5f));
	SWIM_CHECK(std::abs(spot.SpotScale - scale) < 1.0e-4f * scale);
	SWIM_CHECK(std::abs(spot.SpotOffset + std::cos(0.5f) * scale) < 1.0e-4f * scale);
	SWIM_CHECK(spot.Direction[2] == -1.0f);
	// A (nearly) hard cone clamps the delta.
	const auto hard = L::EncodeLight(Spot(0.4999f, 0.5f));
	SWIM_CHECK(std::abs(hard.SpotScale - 1.0f / L::MinSpotConeDelta) < 1.0f);

	// Invalid descs.
	const auto rejects = [](LightDesc desc)
	{
		SWIM_CHECK(!L::ValidateLight(desc).empty());
		SWIM_CHECK_THROWS(L::EncodeLight(desc), std::invalid_argument);
	};
	auto bad = point;
	bad.Range = 0.0f;
	rejects(bad);
	bad = point;
	bad.Intensity = -1.0f;
	rejects(bad);
	bad = point;
	bad.Color[1] = NAN;
	rejects(bad);
	bad = sun;
	bad.Direction = { 0, 0, 0 };
	rejects(bad);
	rejects(Spot(0.5f, 0.5f));
	rejects(Spot(-0.1f, 0.5f));
	rejects(Spot(0.1f, 1.6f));
	bad = point;
	bad.Type = static_cast<LightType>(9);
	rejects(bad);
	sun.Range = 0.0f; // Directional lights ignore range.
	SWIM_CHECK(L::ValidateLight(sun).empty());
}

SWIM_TEST("Render.Lights", "AttenuationFollowsTheInverseSquareLawAndReachesZeroAtRange")
{
	const float inverseRange2 = 1.0f / 100.0f; // Range 10.
	// Far inside the range the window is ~1: pure inverse square.
	SWIM_CHECK(std::abs(L::RangeAttenuation(1.0f, inverseRange2) - (1.0f - 1.0e-4f) * (1.0f - 1.0e-4f)) < 1.0e-6f);
	SWIM_CHECK(std::abs(L::RangeAttenuation(4.0f, inverseRange2) * 4.0f - std::pow(1.0f - 0.0016f, 2.0f)) < 1.0e-6f);
	SWIM_CHECK_EQUAL(L::RangeAttenuation(100.0f, inverseRange2), 0.0f);
	SWIM_CHECK_EQUAL(L::RangeAttenuation(150.0f, inverseRange2), 0.0f);
	// Monotonically decreasing and continuous up to the range.
	float previous = 1.0e9f;
	for (int i = 1; i <= 100; ++i)
	{
		const float d = 0.1f * float(i);
		const float a = L::RangeAttenuation(d * d, inverseRange2);
		SWIM_CHECK(a <= previous);
		previous = a;
	}
	SWIM_CHECK(previous < 1.0e-6f);
	// The distance clamp keeps the light finite at its position.
	SWIM_CHECK_EQUAL(L::RangeAttenuation(0.0f, inverseRange2), 1.0f / L::MinDistanceSquared);

	// Spot falloff: 1 inside the inner cone, 0 outside the outer cone, smooth between.
	const auto spot = L::EncodeLight(Spot(0.2f, 0.5f));
	const auto toLightAt = [](float angle)
	{
		// The light points along -Z; a point below it at `angle` off the axis.
		return L::Float3{ -std::sin(angle), 0.0f, std::cos(angle) };
	};
	SWIM_CHECK((L::SpotAttenuation(spot, toLightAt(0.0f))) == 1.0f);
	SWIM_CHECK((L::SpotAttenuation(spot, toLightAt(0.19f))) == 1.0f);
	SWIM_CHECK((L::SpotAttenuation(spot, toLightAt(0.51f))) == 0.0f);
	const float mid = L::SpotAttenuation(spot, toLightAt(0.35f));
	SWIM_CHECK(mid > 0.0f && mid < 1.0f);
	const float t = (std::cos(0.35f) - std::cos(0.5f)) / (std::cos(0.2f) - std::cos(0.5f));
	SWIM_CHECK(std::abs(mid - t * t) < 1.0e-4f);
	LightDesc point;
	SWIM_CHECK((L::SpotAttenuation(L::EncodeLight(point), toLightAt(2.0f))) == 1.0f);
}

SWIM_TEST("Render.Lights", "EvaluationReturnsDirectionAndRadianceForEveryType")
{
	LightDesc sun;
	sun.Type = LightType::Directional;
	sun.Direction = { 0, -1, 0 };
	sun.Color = { 1, 0.9f, 0.8f };
	sun.Intensity = 3.0f;
	const auto directional = L::EvaluateLight(L::EncodeLight(sun), { 100, -5, 7 });
	SWIM_CHECK((directional.Direction == L::Float3{ 0, 1, 0 }));
	SWIM_CHECK(std::abs(directional.Radiance[1] - 2.7f) < 1.0e-6f);

	LightDesc point;
	point.Position = { 0, 3, 0 };
	point.Intensity = 9.0f;
	point.Range = 100.0f;
	const auto p = L::EvaluateLight(L::EncodeLight(point), { 0, 0, 0 });
	SWIM_CHECK(std::abs(p.Direction[1] - 1.0f) < 1.0e-6f);
	SWIM_CHECK(std::abs(p.Radiance[0] - 9.0f / 9.0f * std::pow(1.0f - std::pow(9.0f / 10000.0f, 2.0f), 2.0f)) < 1.0e-5f);
	SWIM_CHECK((L::EvaluateLight(L::EncodeLight(point), { 0, 200, 0 }).Radiance[0]) == 0.0f); // Out of range.

	auto spotDesc = Spot(0.1f, 0.3f, 20.0f);
	spotDesc.Position = { 0, 5, 0 };
	spotDesc.Direction = { 0, -1, 0 };
	const auto spot = L::EncodeLight(spotDesc);
	SWIM_CHECK(L::EvaluateLight(spot, { 0, 0, 0 }).Radiance[0] > 0.0f);
	SWIM_CHECK((L::EvaluateLight(spot, { 5, 0, 0 }).Radiance[0]) == 0.0f);	// 45 degrees off axis.
	SWIM_CHECK((L::EvaluateLight(spot, { 0, 10, 0 }).Radiance[0]) == 0.0f); // Behind.
	// Directions are unit vectors even at the light.
	SWIM_CHECK(std::abs(Length(L::EvaluateLight(spot, { 0.3f, 1.0f, -0.2f }).Direction) - 1.0f) < 1.0e-5f);
}

SWIM_TEST("Render.Lights", "BoundingSpheresContainEveryLitPointAndSpotSpheresAreTight")
{
	std::mt19937 random(63);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::normal_distribution<float> normal(0.0f, 1.0f);
	for (int trial = 0; trial < 64; ++trial)
	{
		LightDesc desc;
		desc.Type = trial % 3 == 0 ? LightType::Point : LightType::Spot;
		desc.Position = { 10 * unit(random) - 5, 10 * unit(random) - 5, 10 * unit(random) - 5 };
		desc.Direction = { normal(random), normal(random), normal(random) };
		desc.Range = 0.5f + 5.0f * unit(random);
		desc.OuterConeAngle = 0.05f + 1.5f * unit(random);
		desc.InnerConeAngle = desc.OuterConeAngle * 0.5f * unit(random);
		const auto record = L::EncodeLight(desc);
		const auto sphere = L::LightBoundingSphere(record);
		SWIM_CHECK(sphere.Radius <= desc.Range + 1.0e-5f);
		// Every point with non-zero radiance lies inside the sphere.
		for (int i = 0; i < 400; ++i)
		{
			const float r = desc.Range * std::cbrt(unit(random));
			const auto d = Pbr::Normalize({ normal(random), normal(random), normal(random) });
			const L::Float3 point{ desc.Position[0] + d[0] * r, desc.Position[1] + d[1] * r, desc.Position[2] + d[2] * r };
			if (L::EvaluateLight(record, point).Radiance[0] <= 0.0f)
			{
				continue;
			}
			const float dx = point[0] - sphere.Center[0];
			const float dy = point[1] - sphere.Center[1];
			const float dz = point[2] - sphere.Center[2];
			SWIM_CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) <= sphere.Radius * 1.0001f + 1.0e-5f);
		}
	}
	// A narrow spot's sphere is much smaller than its range sphere; a hemisphere spot's is the range sphere.
	const auto narrow = L::LightBoundingSphere(L::EncodeLight(Spot(0.1f, 0.2f, 10.0f)));
	SWIM_CHECK(std::abs(narrow.Radius - 10.0f / (2.0f * std::cos(0.2f))) < 1.0e-4f);
	SWIM_CHECK(std::abs(narrow.Center[2] - (3.0f - narrow.Radius)) < 1.0e-4f);
	const auto wide = L::LightBoundingSphere(L::EncodeLight(Spot(0.1f, 1.2f, 10.0f)));
	SWIM_CHECK(std::abs(wide.Radius - 10.0f * std::sin(1.2f)) < 1.0e-4f);
	const auto hemisphere = L::LightBoundingSphere(L::EncodeLight(Spot(0.1f, 1.5707964f, 10.0f)));
	SWIM_CHECK(std::abs(hemisphere.Radius - 10.0f) < 1.0e-4f);
}

SWIM_TEST("Render.Lights", "BruteForceShadingSumsEveryLightOnce")
{
	std::vector<GpuLightRecord> rows(6);
	GpuLightHeader header;
	header.FirstLocalRow = 2;
	header.LocalCapacity = 4;
	LightDesc sun;
	sun.Type = LightType::Directional;
	sun.Direction = { 0, 0, -1 };
	rows[0] = L::EncodeLight(sun);
	LightDesc point;
	point.Position = { 0, 0, 2 };
	point.Intensity = 4.0f;
	rows[2] = L::EncodeLight(point);
	rows[3] = L::EncodeLight(point);
	rows[1] = rows[4] = L::EncodeLight(point); // Beyond the counts: ignored.
	Pbr::Surface surface;
	surface.BaseColor = { 0.5f, 0.5f, 0.5f };
	surface.Metallic = 0.0f;
	surface.PerceptualRoughness = 0.5f;
	const L::Float3 n{ 0, 0, 1 };
	const L::Float3 v{ 0, 0, 1 };
	const auto brdf = Pbr::EvaluateBrdf(surface, n, v, { 0, 0, 1 });
	header.DirectionalCount = 1;
	header.LocalCount = 2;
	const auto sum = L::ShadeAllLights(rows, header, surface, n, v, { 0, 0, 0 });
	const float pointRadiance = 4.0f * L::RangeAttenuation(4.0f, 1.0f / 100.0f);
	SWIM_CHECK(std::abs(sum[0] - brdf[0] * (1.0f + 2.0f * pointRadiance)) < 1.0e-5f);
	header.DirectionalCount = header.LocalCount = 0;
	SWIM_CHECK((L::ShadeAllLights(rows, header, surface, n, v, { 0, 0, 0 }) == L::Float3{ 0, 0, 0 }));
}
