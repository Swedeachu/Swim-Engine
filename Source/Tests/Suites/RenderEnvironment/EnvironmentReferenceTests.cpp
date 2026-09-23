#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Tests/Fixtures/PbrGalleryFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <filesystem>
#include <random>

using namespace Swim;
using namespace Swim::Render;
namespace Env = Swim::Render::Environment;
namespace Pbr = Swim::Render::StandardPbr;

namespace
{
	bool Near(const Env::Float4& a, const Env::Float4& b, float tolerance)
	{
		for (int c = 0; c < 4; ++c)
		{
			if (std::abs(a[c] - b[c]) > tolerance)
			{
				return false;
			}
		}
		return true;
	}

	Env::Float3 RandomDirection(std::mt19937& random)
	{
		std::normal_distribution<float> normal(0.0f, 1.0f);
		return Env::Normalize({ normal(random), normal(random), normal(random) });
	}

	// A CPU environment built exactly like EnvironmentBuilder's passes.
	Env::EnvironmentProbe BuildProbe(const Env::ProceduralSky& sky, std::uint32_t sourceSize, std::uint32_t prefilteredSize,
		std::uint32_t mips, std::uint32_t samples, std::uint32_t lutSize)
	{
		const auto source = Env::BuildSkyCube(sky, sourceSize);
		return Env::EnvironmentProbe(Env::ProjectIrradianceSh(source, source.GetMipCount() - 1),
			Env::BuildPrefilteredCube(source, prefilteredSize, mips, samples), Env::BuildBrdfLut(lutSize, 128));
	}
} // namespace

SWIM_TEST("Render.Environment.CubeImage", "LayoutMipsAndBoxFilterMatchTheGpuContract")
{
	SWIM_CHECK_THROWS(Env::CubeImage(12), std::invalid_argument);
	SWIM_CHECK_THROWS(Env::CubeImage(8, 5), std::invalid_argument);
	const Env::CubeImage full(8);
	SWIM_CHECK_EQUAL(full.GetMipCount(), 4u);
	SWIM_CHECK_EQUAL(full.GetMipSize(3), 1u);
	SWIM_CHECK_EQUAL(Env::FullCubeMipCount(128), 8u);
	SWIM_CHECK_EQUAL(Env::EnvironmentSourceMipCount(128), 6u); // 128 .. 4.
	SWIM_CHECK_EQUAL(Env::EnvironmentSourceMipCount(16), 3u);

	Env::CubeImage cube(4);
	std::mt19937 random(3);
	std::uniform_real_distribution<float> unit(0.0f, 4.0f);
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		for (auto& texel : cube.Face(0, face))
		{
			texel = { unit(random), unit(random), unit(random), 1.0f };
		}
	}
	cube.GenerateMips();
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		for (std::uint32_t y = 0; y < 2; ++y)
		{
			for (std::uint32_t x = 0; x < 2; ++x)
			{
				for (int c = 0; c < 4; ++c)
				{
					const float expected = 0.25f *
						(cube.Texel(0, face, 2 * x, 2 * y)[c] + cube.Texel(0, face, 2 * x + 1, 2 * y)[c] +
							cube.Texel(0, face, 2 * x, 2 * y + 1)[c] + cube.Texel(0, face, 2 * x + 1, 2 * y + 1)[c]);
					SWIM_CHECK(std::abs(cube.Texel(1, face, x, y)[c] - expected) < 1.0e-6f);
				}
			}
		}
		// Point and trilinear samples at a texel center return the texel.
		for (std::uint32_t y = 0; y < 4; ++y)
		{
			for (std::uint32_t x = 0; x < 4; ++x)
			{
				const auto d = Env::CubeTexelDirection(face, x, y, 4);
				SWIM_CHECK((cube.SampleNearest(d, 0.0f) == cube.Texel(0, face, x, y)));
				SWIM_CHECK(Near(cube.SampleTrilinear(d, 0.0f), cube.Texel(0, face, x, y), 1.0e-5f));
			}
		}
	}
	// Vulkan's nearest-mip rule rounds half down; trilinear blends the two mips.
	Env::CubeImage levels(4);
	for (std::uint32_t mip = 0; mip < levels.GetMipCount(); ++mip)
	{
		for (std::uint32_t face = 0; face < 6; ++face)
		{
			for (auto& texel : levels.Face(mip, face))
			{
				texel = { float(mip), 0, 0, 1 };
			}
		}
	}
	const Env::Float3 d{ 0.3f, 0.2f, 1.0f };
	SWIM_CHECK(levels.SampleNearest(d, 0.5f)[0] == 0.0f);
	SWIM_CHECK(levels.SampleNearest(d, 0.51f)[0] == 1.0f);
	SWIM_CHECK(levels.SampleNearest(d, 7.0f)[0] == 2.0f);
	SWIM_CHECK(std::abs(levels.SampleTrilinear(d, 1.25f)[0] - 1.25f) < 1.0e-5f);
	SWIM_CHECK(levels.SampleTrilinear(d, -1.0f)[0] == 0.0f);
	SWIM_CHECK(levels.SampleTrilinear(d, 9.0f)[0] == 2.0f);
}

SWIM_TEST("Render.Environment.CubeImage", "TrilinearSamplingIsSeamlessAcrossFacesAndCorners")
{
	// Each face a distinct constant: exactly on an edge the bilinear footprint is half
	// in each face, and at a cube corner the result is the average of the three faces.
	Env::CubeImage cube(8, 1);
	const std::array<float, 6> values{ 1, 2, 4, 8, 16, 32 };
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		for (auto& texel : cube.Face(0, face))
		{
			texel = { values[face], 0, 0, 1 };
		}
	}
	SWIM_CHECK(std::abs(cube.SampleTrilinear({ 1, 0, 1 }, 0)[0] - 0.5f * (1 + 16)) < 1.0e-5f);	// +X | +Z
	SWIM_CHECK(std::abs(cube.SampleTrilinear({ 0, 1, -1 }, 0)[0] - 0.5f * (4 + 32)) < 1.0e-5f); // +Y | -Z
	SWIM_CHECK(std::abs(cube.SampleTrilinear({ -1, -1, 0 }, 0)[0] - 0.5f * (2 + 8)) < 1.0e-5f); // -X | -Y
	SWIM_CHECK(std::abs(cube.SampleTrilinear({ 1, 1, 1 }, 0)[0] - (1 + 4 + 16) / 3.0f) < 1.0e-5f);
	SWIM_CHECK(std::abs(cube.SampleTrilinear({ -1, -1, -1 }, 0)[0] - (2 + 8 + 32) / 3.0f) < 1.0e-5f);
	// Away from the edges only the face contributes.
	SWIM_CHECK(cube.SampleTrilinear({ 0.2f, -0.1f, 1 }, 0)[0] == 16.0f);
	// FetchSeamless resolves one step outside a face to the neighbor's edge texel.
	SWIM_CHECK(cube.FetchSeamless(0, 4, 8, 3)[0] == 1.0f);	// Right of +Z is +X.
	SWIM_CHECK(cube.FetchSeamless(0, 4, -1, 3)[0] == 2.0f); // Left of +Z is -X.
	SWIM_CHECK(cube.FetchSeamless(0, 4, 3, -1)[0] == 4.0f); // Above +Z is +Y.
	SWIM_CHECK(cube.FetchSeamless(0, 4, 3, 8)[0] == 8.0f);	// Below +Z is -Y.
}

SWIM_TEST("Render.Environment.Sky", "ProceduralSkyIsContinuousAndPacksItsConstants")
{
	Env::ProceduralSky sky;
	const auto horizonAbove = sky.Evaluate({ 1, 1.0e-6f, 0 });
	const auto horizonBelow = sky.Evaluate({ 1, -1.0e-6f, 0 });
	for (int c = 0; c < 3; ++c)
	{
		SWIM_CHECK(std::abs(horizonAbove[c] - horizonBelow[c]) < 1.0e-4f);
		SWIM_CHECK(std::abs(horizonAbove[c] - sky.HorizonColor[c]) < 1.0e-4f);
	}
	const auto zenith = sky.Evaluate({ 0, 1, 0 });
	const auto sunLobeAtZenith = sky.SunColor[0] * std::pow(Env::Normalize(sky.SunDirection)[1], sky.SunSharpness);
	SWIM_CHECK(std::abs(zenith[0] - (sky.ZenithColor[0] + sunLobeAtZenith)) < 1.0e-4f);
	const auto ground = sky.Evaluate({ 0, -1, 0 });
	SWIM_CHECK(std::abs(ground[2] - sky.GroundColor[2]) < 1.0e-6f);
	// The sun is the brightest direction.
	const auto sun = sky.Evaluate(sky.SunDirection);
	std::mt19937 random(5);
	for (int i = 0; i < 64; ++i)
	{
		SWIM_CHECK(sky.Evaluate(RandomDirection(random))[0] <= sun[0]);
	}
	// Intensity scales everything; Uniform is the white furnace.
	auto brighter = sky;
	brighter.Intensity = 2.5f;
	SWIM_CHECK(std::abs(brighter.Evaluate({ 0.3f, 0.2f, 0.1f })[1] - 2.5f * sky.Evaluate({ 0.3f, 0.2f, 0.1f })[1]) < 1.0e-5f);
	const auto furnace = Env::ProceduralSky::Uniform(0.75f);
	for (int i = 0; i < 16; ++i)
	{
		SWIM_CHECK((furnace.Evaluate(RandomDirection(random)) == Env::Float3{ 0.75f, 0.75f, 0.75f }));
	}

	const auto constants = Env::MakeProceduralSkyConstants(sky, 3, 64);
	SWIM_CHECK_EQUAL(constants.Face, 3u);
	SWIM_CHECK_EQUAL(constants.Size, 64u);
	SWIM_CHECK_EQUAL(constants.Zenith[3], sky.Intensity);
	SWIM_CHECK_EQUAL(constants.SunDirection[3], sky.SunSharpness);
	SWIM_CHECK(std::abs(constants.SunDirection[0] * constants.SunDirection[0] + constants.SunDirection[1] * constants.SunDirection[1] +
				   constants.SunDirection[2] * constants.SunDirection[2] - 1.0f) < 1.0e-6f);
	SWIM_CHECK_EQUAL(constants.Ground[1], sky.GroundColor[1]);

	// The reference sky cube: mip 0 holds the sky at texel centers, the chain stops at 4x4.
	const auto cube = Env::BuildSkyCube(sky, 16);
	SWIM_CHECK_EQUAL(cube.GetMipCount(), 3u);
	const auto expected = sky.Evaluate(Env::CubeTexelDirection(4, 5, 9, 16));
	SWIM_CHECK(std::abs(cube.Texel(0, 4, 5, 9)[0] - expected[0]) < 1.0e-6f);
	SWIM_CHECK_EQUAL(cube.Texel(0, 4, 5, 9)[3], 1.0f);
}

SWIM_TEST("Render.Environment.Prefilter", "PrefilteringPreservesUniformRadianceAndConvergesToTheGgxLobe")
{
	// A uniform environment prefilters to itself at every roughness (normalized weights).
	const auto uniform = Env::BuildSkyCube(Env::ProceduralSky::Uniform(0.6f), 16);
	for (const float roughness : { 0.0f, 0.3f, 1.0f })
	{
		const auto value = Env::PrefilterDirection(uniform, { 0.2f, 0.7f, -0.3f }, roughness, 32);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(value[c] - 0.6f) < 1.0e-5f);
		}
	}
	// Roughness 0 is the lod-0 sample.
	Env::ProceduralSky sky;
	const auto source = Env::BuildSkyCube(sky, 32);
	const Env::Float3 d = Env::Normalize({ 0.1f, 0.4f, 0.8f });
	const auto mirror = Env::PrefilterDirection(source, d, 0.0f, 64);
	const auto sampled = source.SampleTrilinear(d, 0.0f);
	SWIM_CHECK(mirror[0] == sampled[0] && mirror[1] == sampled[1] && mirror[2] == sampled[2]);
	const auto nearest = Env::PrefilterDirection(source, d, 0.0f, 64, Env::CubeSampling::Nearest);
	SWIM_CHECK((nearest[0] == source.SampleNearest(d, 0.0f)[0]));

	// Against brute force: the split-sum prefilter is E[L N.L] / E[N.L] over light
	// directions drawn with pdf D(h) N.H / (4 V.H), N = V = R; integrate that over
	// every source texel. Filtered importance sampling reads blurrier mips, so the
	// agreement is a few percent, tightening with the sample count.
	for (const float roughness : { 0.5f, 0.8f, 1.0f })
	{
		const float alpha = roughness * roughness;
		for (const auto& direction : { Env::Float3{ 0, 1, 0 }, Env::Float3{ 0.15f, 0.3f, 1.0f }, Env::Float3{ 1, -0.2f, 0.1f } })
		{
			const auto n = Env::Normalize(direction);
			std::array<double, 3> sum{};
			double weight = 0.0;
			const std::uint32_t size = source.GetSize();
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				for (std::uint32_t y = 0; y < size; ++y)
				{
					for (std::uint32_t x = 0; x < size; ++x)
					{
						const auto l = Env::CubeTexelDirection(face, x, y, size);
						const float nDotL = Env::Dot(n, l);
						if (nDotL <= 0.0f)
						{
							continue;
						}
						const auto h = Env::Normalize({ n[0] + l[0], n[1] + l[1], n[2] + l[2] });
						const float nDotH = Env::Dot(n, h);
						const double pdf = Pbr::DistributionGgx(nDotH, alpha) * nDotH / (4.0 * Env::Dot(n, h));
						const double w = pdf * nDotL * Env::CubeTexelSolidAngle(x, y, size);
						for (int c = 0; c < 3; ++c)
						{
							sum[c] += source.Texel(0, face, x, y)[c] * w;
						}
						weight += w;
					}
				}
			}
			const auto estimate = Env::PrefilterDirection(source, n, roughness, 1024);
			for (int c = 0; c < 3; ++c)
			{
				const double exact = sum[c] / weight;
				SWIM_CHECK(std::abs(estimate[c] - exact) < 0.04 * exact + 0.01);
			}
		}
	}

	// BuildPrefilteredCube stores mip m at roughness m / (mips - 1).
	const auto prefiltered = Env::BuildPrefilteredCube(source, 8, 4, 16);
	SWIM_CHECK_EQUAL(prefiltered.GetMipCount(), 4u);
	const auto texelDirection = Env::CubeTexelDirection(2, 1, 1, 4);
	const auto expected = Env::PrefilterDirection(source, texelDirection, 1.0f / 3.0f, 16);
	SWIM_CHECK_EQUAL(prefiltered.Texel(1, 2, 1, 1)[1], expected[1]);
}

SWIM_TEST("Render.Environment.BrdfLut", "LutTexelsAreTheSplitSumAtTexelCenters")
{
	const auto lut = Env::BuildBrdfLut(8, 64);
	SWIM_CHECK_EQUAL(lut.Width, 8u);
	SWIM_CHECK_EQUAL(lut.Texels.size(), std::size_t(64));
	const auto ab = Env::IntegrateBrdf(5.5f / 8.0f, 2.5f / 8.0f, 64);
	SWIM_CHECK_EQUAL(lut.At(5, 2)[0], ab[0]);
	SWIM_CHECK_EQUAL(lut.At(5, 2)[1], ab[1]);
	SWIM_CHECK_EQUAL(lut.At(5, 2)[3], 1.0f);
	// Bilinear sampling: centers return texels, midpoints average, edges clamp.
	SWIM_CHECK(Near(lut.SampleBilinear(5.5f / 8.0f, 2.5f / 8.0f), lut.At(5, 2), 1.0e-6f));
	const auto mid = lut.SampleBilinear(6.0f / 8.0f, 2.5f / 8.0f);
	SWIM_CHECK(std::abs(mid[0] - 0.5f * (lut.At(5, 2)[0] + lut.At(6, 2)[0])) < 1.0e-6f);
	SWIM_CHECK(Near(lut.SampleBilinear(0.0f, 0.0f), lut.At(0, 0), 1.0e-6f));
	SWIM_CHECK(Near(lut.SampleBilinear(1.0f, 1.0f), lut.At(7, 7), 1.0e-6f));
}

SWIM_TEST("Render.Environment.Shading", "SplitSumIblConservesEnergyInTheFurnaceAndKeepsDirectShadingUnchanged")
{
	std::mt19937 random(64);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	// White dielectric in a uniform environment of radiance 1: diffuse (1 - E) + specular E = 1.
	for (int i = 0; i < 64; ++i)
	{
		Pbr::ResolvedSurface surface;
		surface.BaseColor = { 1, 1, 1 };
		surface.Metallic = 0.0f;
		surface.PerceptualRoughness = unit(random);
		const float nDotV = 0.05f + 0.95f * unit(random);
		const Pbr::Float3 view{ std::sqrt(1.0f - nDotV * nDotV), 0, nDotV };
		const auto ab = Env::IntegrateBrdf(nDotV, surface.PerceptualRoughness, 128);
		const Pbr::EnvironmentTerms furnace{ { 1, 1, 1 }, { 1, 1, 1 }, ab[0], ab[1] };
		const auto color = Pbr::EvaluateEnvironment(surface, view, furnace);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(color[c] - 1.0f) < 1.0e-5f);
		}
		// A white metal reflects A + B (<= 1: single scattering loses energy when rough).
		surface.Metallic = 1.0f;
		const auto metal = Pbr::EvaluateEnvironment(surface, view, furnace);
		SWIM_CHECK(std::abs(metal[0] - (ab[0] + ab[1])) < 1.0e-5f);
		SWIM_CHECK(metal[0] <= 1.001f); // Up to Monte Carlo noise near the mirror limit.
		// Occlusion scales the whole environment term; intensity is linear.
		surface.Occlusion = 0.25f;
		SWIM_CHECK(std::abs(Pbr::EvaluateEnvironment(surface, view, furnace)[1] - 0.25f * metal[1]) < 1.0e-5f);
		const Pbr::EnvironmentTerms doubled{ { 2, 2, 2 }, { 2, 2, 2 }, ab[0], ab[1] };
		SWIM_CHECK(std::abs(Pbr::EvaluateEnvironment(surface, view, doubled)[2] - 0.5f * metal[2]) < 1.0e-5f);
	}
	// Reflect mirrors about the normal.
	const auto r = Pbr::Reflect(Pbr::Normalize({ 1, 0, 1 }), { 0, 0, 1 });
	SWIM_CHECK(std::abs(r[0] + std::sqrt(0.5f)) < 1.0e-6f && std::abs(r[2] - std::sqrt(0.5f)) < 1.0e-6f);

	// Shade is Resolve + ShadeResolved without IBL, bit for bit; IBL adds on top.
	for (int i = 0; i < 64; ++i)
	{
		Pbr::Parameters parameters;
		parameters.BaseColorFactor = { unit(random), unit(random), unit(random), 1 };
		parameters.MetallicFactor = unit(random);
		parameters.RoughnessFactor = unit(random);
		parameters.EmissiveFactor = { unit(random), 0, 0 };
		Pbr::Texels texels;
		texels.Occlusion = unit(random);
		texels.TangentNormal = Pbr::Float3{ 0.2f, -0.1f, 0.97f };
		Pbr::Lighting lighting;
		lighting.LightDirection = Pbr::Normalize({ unit(random), unit(random), 1 });
		lighting.LightRadiance = { 2, 2, 2 };
		lighting.Ambient = { 0.1f, 0.1f, 0.1f };
		const auto shaded = Pbr::Shade(parameters, texels, {}, lighting);
		const auto surface = Pbr::Resolve(parameters, texels, {});
		SWIM_REQUIRE(shaded && surface);
		SWIM_CHECK((*shaded == Pbr::ShadeResolved(*surface, lighting, nullptr)));
		const Pbr::EnvironmentTerms terms{ { 0.3f, 0.3f, 0.3f }, { 0.5f, 0.4f, 0.3f }, 0.7f, 0.1f };
		const auto withIbl = Pbr::ShadeResolved(*surface, lighting, &terms);
		const auto ibl = Pbr::EvaluateEnvironment(*surface, lighting.View, terms);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(withIbl[c] - ((*shaded)[c] + ibl[c])) < 1.0e-5f);
		}
		SWIM_CHECK_EQUAL(withIbl[3], (*shaded)[3]);
	}
	// Alpha-masked pixels resolve to nothing.
	Pbr::Parameters masked;
	masked.Flags = Pbr::FlagAlphaMask;
	masked.BaseColorFactor[3] = 0.2f;
	SWIM_CHECK(!Pbr::Resolve(masked, {}, {}));
}

SWIM_TEST("Render.Environment.Probe", "LookupsApplyIntensityRotationAndTheRoughnessLod")
{
	SWIM_CHECK_THROWS(Env::EnvironmentProbe({}, Env::CubeImage(), Env::Image2D{}), std::invalid_argument);
	// Uniform environment: every lookup returns the radiance times the intensity.
	const auto uniform = BuildProbe(Env::ProceduralSky::Uniform(2.0f), 16, 8, 4, 32, 16);
	std::mt19937 random(9);
	for (int i = 0; i < 16; ++i)
	{
		Pbr::ResolvedSurface surface;
		surface.Normal = RandomDirection(random);
		surface.PerceptualRoughness = float(i) / 15.0f;
		const auto terms = uniform.Lookup(surface, { 0, 0, 1 }, { 0.5f, float(i) });
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(terms.Irradiance[c] - 1.0f) < 2.0e-3f);
			SWIM_CHECK(std::abs(terms.Prefiltered[c] - 1.0f) < 1.0e-4f);
		}
	}

	// Rotating the environment by theta equals looking up the rotated direction.
	const auto probe = BuildProbe(Env::ProceduralSky{}, 32, 16, 5, 32, 16);
	Pbr::ResolvedSurface surface;
	surface.Normal = Env::Normalize({ 0.3f, 0.5f, 0.8f });
	surface.PerceptualRoughness = 0.0f;
	const Env::Float3 view{ 0, 0, 1 };
	const auto rotated = probe.Lookup(surface, view, { 1.0f, 0.7f });
	const auto reflected = Pbr::Reflect(view, surface.Normal);
	const auto direct = probe.GetPrefiltered().SampleTrilinear(
		Env::RotateEnvironmentLookup(reflected, 0.7f), Env::PrefilterLodForRoughness(0.0f, probe.GetPrefiltered().GetMipCount()));
	SWIM_CHECK(std::abs(rotated.Prefiltered[0] - direct[0]) < 1.0e-6f);
	const auto irradiance = probe.GetIrradiance().Evaluate(Env::RotateEnvironmentLookup(surface.Normal, 0.7f));
	SWIM_CHECK(std::abs(rotated.Irradiance[1] - irradiance[1]) < 1.0e-6f);
	// The LUT is read at (N.V, clamped roughness).
	const auto ab = probe.GetBrdfLut().SampleBilinear(Env::Dot(surface.Normal, view), Pbr::MinPerceptualRoughness);
	SWIM_CHECK_EQUAL(rotated.BrdfScale, ab[0]);
	SWIM_CHECK_EQUAL(rotated.BrdfBias, ab[1]);
	// Rougher surfaces read blurrier mips: the sun's mirror image dims as roughness grows.
	surface.Normal = Env::Normalize({ 0.075f, 0.15f, 1.0f }); // Reflects the view toward the sun.
	float previous = 1.0e9f;
	for (const float roughness : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
	{
		surface.PerceptualRoughness = roughness;
		const float value = probe.Lookup(surface, view).Prefiltered[0];
		SWIM_CHECK(value < previous);
		previous = value;
	}
}

SWIM_TEST("Render.PbrGallery", "CpuReferenceMeetsTheGalleryExpectations")
{
	namespace Gallery = Testing::PbrGallery;
	const auto layout = Gallery::MakeLayout(32);
	SWIM_CHECK_EQUAL(layout.Width, 192u);
	SWIM_CHECK_EQUAL(layout.Height, 128u);
	SWIM_CHECK_EQUAL(layout.Spheres.size(), std::size_t(24));
	SWIM_CHECK_EQUAL(layout.Spheres[5].Roughness, 1.0f);

	// Furnace: a uniform environment of radiance 1 and no direct light. White
	// dielectric spheres render exactly 1 everywhere; nothing exceeds 1.
	const auto furnaceProbe = BuildProbe(Env::ProceduralSky::Uniform(1.0f), 16, 16, 5, 32, 32);
	const auto furnace = Gallery::Render(layout, furnaceProbe, {});
	std::uint32_t covered = 0;
	for (std::uint32_t y = 0; y < layout.Height; ++y)
	{
		for (std::uint32_t x = 0; x < layout.Width; ++x)
		{
			const auto& pixel = furnace[std::size_t(y) * layout.Width + x];
			const auto coverage = Gallery::Cover(layout, x, y);
			if (!coverage)
			{
				SWIM_CHECK((pixel == Gallery::Float4{ 0, 0, 0, 0 }));
				continue;
			}
			++covered;
			SWIM_CHECK(pixel[0] <= 1.0005f && pixel[1] <= 1.0005f && pixel[2] <= 1.0005f);
			SWIM_CHECK_EQUAL(pixel[3], 1.0f);
			if (coverage->Sphere / layout.Columns == Gallery::WhiteDielectricRow)
			{
				SWIM_CHECK(std::abs(pixel[0] - 1.0f) < 2.0e-3f && std::abs(pixel[2] - 1.0f) < 2.0e-3f);
			}
		}
	}
	SWIM_CHECK(covered > 24u * 400u);
	// Image dumps (SWIM_PBR_GALLERY_DUMP in the native smoke): a PFM and a 24-bit BMP.
	const auto stem = (std::filesystem::temp_directory_path() / "swim-pbr-gallery-test").string();
	SWIM_REQUIRE(Gallery::WriteImages(stem, furnace, layout.Width, layout.Height));
	SWIM_CHECK_EQUAL(std::filesystem::file_size(stem + ".bmp"), std::uintmax_t(54 + 192 * 3 * 128));
	SWIM_CHECK_EQUAL(std::filesystem::file_size(stem + ".pfm"), std::uintmax_t(std::string("PF\n192 128\n-1.0\n").size() + 192 * 128 * 12));
	std::filesystem::remove(stem + ".bmp");
	std::filesystem::remove(stem + ".pfm");

	// Sky only: the metal row's peak brightness falls monotonically with roughness
	// (the sun's reflection spreads). The direct light then adds a highlight.
	const auto probe = BuildProbe(Env::ProceduralSky{}, 32, 16, 5, 64, 32);
	Gallery::Frame lit;
	lit.LightDirection = Env::ProceduralSky{}.SunDirection;
	const auto peaks = [&](const std::vector<Gallery::Float4>& pixels)
	{
		std::vector<float> peak(layout.Spheres.size(), 0.0f);
		for (std::uint32_t y = 0; y < layout.Height; ++y)
		{
			for (std::uint32_t x = 0; x < layout.Width; ++x)
			{
				if (const auto coverage = Gallery::Cover(layout, x, y))
				{
					const auto& pixel = pixels[std::size_t(y) * layout.Width + x];
					SWIM_CHECK(std::isfinite(pixel[0]) && pixel[0] >= 0.0f && pixel[1] >= 0.0f && pixel[2] >= 0.0f);
					peak[coverage->Sphere] = std::max(peak[coverage->Sphere], Gallery::Luminance(pixel));
				}
			}
		}
		return peak;
	};
	const auto skyPeaks = peaks(Gallery::Render(layout, probe, lit));
	for (std::uint32_t column = 1; column < layout.Columns; ++column)
	{
		SWIM_CHECK(skyPeaks[column] < skyPeaks[column - 1]); // Gold.
	}
	lit.LightRadiance = { 3, 3, 3 };
	const auto image = Gallery::Render(layout, probe, lit);
	const auto litPeaks = peaks(image);
	for (std::uint32_t sphere = 0; sphere < layout.Spheres.size(); ++sphere)
	{
		SWIM_CHECK(litPeaks[sphere] > skyPeaks[sphere]);
	}
	// Red plastic stays red, gold stays warm.
	const auto centerOf = [&](std::uint32_t sphere)
	{
		const auto& s = layout.Spheres[sphere];
		return image[std::size_t(s.CenterY) * layout.Width + std::size_t(s.CenterX)];
	};
	SWIM_CHECK(centerOf(layout.Columns + 3)[0] > 4.0f * centerOf(layout.Columns + 3)[2]);
	SWIM_CHECK(centerOf(3)[0] > centerOf(3)[2]);
	// Occlusion darkens the copper row relative to an unoccluded copy.
	auto unoccluded = layout;
	for (auto& sphere : unoccluded.Spheres)
	{
		sphere.Occlusion = 1.0f;
	}
	lit.LightRadiance = { 0, 0, 0 };
	const auto sphere = 3 * layout.Columns + 2;
	const auto coverage =
		Gallery::Cover(layout, std::uint32_t(layout.Spheres[sphere].CenterX), std::uint32_t(layout.Spheres[sphere].CenterY));
	SWIM_REQUIRE(coverage.has_value());
	const auto occluded = Gallery::ShadePixel(layout, probe, lit, *coverage);
	const auto open = Gallery::ShadePixel(unoccluded, probe, lit, *coverage);
	SWIM_CHECK(std::abs(occluded[1] - 0.6f * open[1]) < 1.0e-5f);
}
