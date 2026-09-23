#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <random>
#include <set>

using namespace Swim;
using namespace Swim::Render;
namespace Env = Swim::Render::Environment;
namespace Pbr = Swim::Render::StandardPbr;

namespace
{
	float Length(const Env::Float3& v)
	{
		return std::sqrt(Env::Dot(v, v));
	}

	Env::Float3 RandomDirection(std::mt19937& random)
	{
		std::normal_distribution<float> normal(0.0f, 1.0f);
		return Env::Normalize({ normal(random), normal(random), normal(random) });
	}

	// A CPU cube whose texels hold f(direction) at mip 0.
	template <typename F> Env::CubeImage CubeOf(std::uint32_t size, F&& f)
	{
		Env::CubeImage cube(size, 1);
		for (std::uint32_t face = 0; face < Env::CubeFaceCount; ++face)
		{
			for (std::uint32_t y = 0; y < size; ++y)
			{
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const auto value = f(Env::CubeTexelDirection(face, x, y, size));
					cube.Texel(0, face, x, y) = { value[0], value[1], value[2], 1.0f };
				}
			}
		}
		return cube;
	}
} // namespace

SWIM_TEST("Render.Environment.Math", "CubeFaceTableRoundTripsEveryTexelAndFollowsTheVulkanAxes")
{
	// Face centers are the six axes in layer order +X, -X, +Y, -Y, +Z, -Z.
	const std::array<Env::Float3, 6> axes{ { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } } };
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		SWIM_CHECK((Env::CubeFaceDirection(face, 0, 0) == axes[face]));
	}
	// Vulkan's table: +X face, s grows toward -Z and t toward -Y; +Y face, t grows toward +Z.
	SWIM_CHECK((Env::CubeFaceDirection(0, 1, 1) == Env::Float3{ 1, -1, -1 }));
	SWIM_CHECK((Env::CubeFaceDirection(2, 1, 1) == Env::Float3{ 1, 1, 1 }));
	SWIM_CHECK((Env::CubeFaceDirection(4, 1, 1) == Env::Float3{ 1, -1, 1 }));
	SWIM_CHECK((Env::CubeFaceDirection(5, 1, 1) == Env::Float3{ -1, -1, -1 }));
	SWIM_CHECK_THROWS(Env::CubeFaceDirection(6, 0, 0), std::out_of_range);

	for (const std::uint32_t size : { 1u, 2u, 7u, 16u })
	{
		for (std::uint32_t face = 0; face < 6; ++face)
		{
			for (std::uint32_t y = 0; y < size; ++y)
			{
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const auto direction = Env::CubeTexelDirection(face, x, y, size);
					SWIM_CHECK(std::abs(Length(direction) - 1.0f) < 1.0e-6f);
					const auto coordinate = Env::DirectionToCube(direction);
					SWIM_CHECK_EQUAL(coordinate.Face, face);
					SWIM_CHECK(std::abs(coordinate.S - (2.0f * (float(x) + 0.5f) / float(size) - 1.0f)) < 1.0e-5f);
					SWIM_CHECK(std::abs(coordinate.T - (2.0f * (float(y) + 0.5f) / float(size) - 1.0f)) < 1.0e-5f);
				}
			}
		}
	}
	// Ties prefer X, then Y.
	SWIM_CHECK_EQUAL(Env::DirectionToCube({ 1, 1, 1 }).Face, 0u);
	SWIM_CHECK_EQUAL(Env::DirectionToCube({ 0, -1, 1 }).Face, 3u);
}

SWIM_TEST("Render.Environment.Math", "TexelSolidAnglesAreExactAndSumToFourPi")
{
	for (const std::uint32_t size : { 1u, 2u, 5u, 16u, 64u })
	{
		double sum = 0.0;
		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				const float solidAngle = Env::CubeTexelSolidAngle(x, y, size);
				SWIM_CHECK(solidAngle > 0.0f);
				sum += solidAngle;
			}
		}
		SWIM_CHECK(std::abs(sum * 6.0 - 4.0 * double(Env::Pi)) < 2.0e-5);
	}
	// A small texel matches the differential form dA / (1 + s^2 + t^2)^1.5; corners are smallest.
	const std::uint32_t size = 128;
	const float s = 2.0f * (40.5f / float(size)) - 1.0f;
	const float t = 2.0f * (90.5f / float(size)) - 1.0f;
	const float differential = (2.0f / float(size)) * (2.0f / float(size)) / std::pow(1.0f + s * s + t * t, 1.5f);
	SWIM_CHECK(std::abs(Env::CubeTexelSolidAngle(40, 90, size) / differential - 1.0f) < 1.0e-3f);
	SWIM_CHECK(Env::CubeTexelSolidAngle(0, 0, size) < Env::CubeTexelSolidAngle(64, 64, size));
}

SWIM_TEST("Render.Environment.Math", "HammersleyPointsAreTheRadicalInverseSequence")
{
	SWIM_CHECK((Env::Hammersley(0, 8) == Env::Float2{ 0.0f, 0.0f }));
	SWIM_CHECK((Env::Hammersley(1, 8) == Env::Float2{ 0.125f, 0.5f }));
	SWIM_CHECK((Env::Hammersley(2, 8) == Env::Float2{ 0.25f, 0.25f }));
	SWIM_CHECK((Env::Hammersley(3, 8) == Env::Float2{ 0.375f, 0.75f }));
	SWIM_CHECK((Env::Hammersley(5, 8) == Env::Float2{ 0.625f, 0.625f }));
	// For n = 2^k the second coordinates are a permutation of i / n: perfectly stratified.
	std::set<float> seen;
	for (std::uint32_t i = 0; i < 256; ++i)
	{
		const auto point = Env::Hammersley(i, 256);
		SWIM_CHECK(point[0] >= 0.0f && point[0] < 1.0f && point[1] >= 0.0f && point[1] < 1.0f);
		const float scaled = point[1] * 256.0f;
		SWIM_CHECK(scaled == std::floor(scaled));
		seen.insert(point[1]);
	}
	SWIM_CHECK_EQUAL(seen.size(), std::size_t(256));
}

SWIM_TEST("Render.Environment.Math", "GgxImportanceSamplesFollowTheNormalizedDistribution")
{
	for (const float alpha : { 0.1f, 0.35f, 0.8f, 1.0f })
	{
		constexpr std::uint32_t count = 8192;
		constexpr int bins = 8;
		std::array<float, bins> histogram{};
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const auto h = Env::ImportanceSampleGgx(Env::Hammersley(i, count), alpha);
			SWIM_CHECK(std::abs(Length(h) - 1.0f) < 1.0e-5f);
			SWIM_CHECK(h[2] > 0.0f);
			histogram[std::min(int(h[2] * bins), bins - 1)] += 1.0f / float(count);
		}
		// P(cos(theta_h) in bin) = integral of D(mu) mu 2 pi dmu over the bin: the
		// sampler's pdf is D(h) cos(theta_h), and D is normalized (the bins sum to 1).
		float total = 0.0f;
		for (int bin = 0; bin < bins; ++bin)
		{
			double expected = 0.0;
			constexpr int steps = 4000;
			const double low = double(bin) / bins;
			const double width = 1.0 / bins / steps;
			for (int step = 0; step < steps; ++step)
			{
				const double mu = low + (step + 0.5) * width;
				expected += Pbr::DistributionGgx(float(mu), alpha) * mu * 2.0 * Pbr::Pi * width;
			}
			total += float(expected);
			SWIM_CHECK(std::abs(histogram[bin] - float(expected)) < 2.0e-3f + 0.01f * float(expected));
		}
		SWIM_CHECK(std::abs(total - 1.0f) < 2.0e-3f);
	}
}

SWIM_TEST("Render.Environment.Math", "TangentFrameCarriesZToTheNormalAndPreservesLength")
{
	std::mt19937 random(61);
	std::vector<Env::Float3> normals{ { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 0.9995f, 0.0316f } };
	for (int i = 0; i < 64; ++i)
	{
		normals.push_back(RandomDirection(random));
	}
	for (auto normal : normals)
	{
		normal = Env::Normalize(normal);
		const auto z = Env::TangentToWorld({ 0, 0, 1 }, normal);
		SWIM_CHECK(std::abs(Env::Dot(z, normal) - 1.0f) < 1.0e-5f);
		const auto x = Env::TangentToWorld({ 1, 0, 0 }, normal);
		const auto y = Env::TangentToWorld({ 0, 1, 0 }, normal);
		SWIM_CHECK(std::abs(Env::Dot(x, normal)) < 1.0e-5f);
		SWIM_CHECK(std::abs(Env::Dot(y, normal)) < 1.0e-5f);
		SWIM_CHECK(std::abs(Env::Dot(x, y)) < 1.0e-5f);
		SWIM_CHECK(std::abs(Length(Env::TangentToWorld({ 0.6f, -0.48f, 0.64f }, normal)) - 1.0f) < 1.0e-5f);
	}
}

SWIM_TEST("Render.Environment.Math", "SplitSumBrdfMatchesTheBruteForceDirectionalAlbedo")
{
	// With F0 = 1 the specular directional albedo is A + B; with F0 = 0 it is B. Both
	// are integrated over the hemisphere with StandardPbr's own D and Vis terms.
	for (const float nDotV : { 0.15f, 0.5f, 0.9f })
	{
		for (const float roughness : { 0.35f, 0.6f, 1.0f })
		{
			const float alpha = roughness * roughness;
			const Pbr::Float3 view{ std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV };
			double full = 0.0;
			double bias = 0.0;
			constexpr int steps = 384;
			const double dTheta = 0.5 * Pbr::Pi / steps;
			const double dPhi = 2.0 * Pbr::Pi / steps;
			for (int i = 0; i < steps; ++i)
			{
				const double theta = (i + 0.5) * dTheta;
				for (int j = 0; j < steps; ++j)
				{
					const double phi = (j + 0.5) * dPhi;
					const Pbr::Float3 light{ float(std::sin(theta) * std::cos(phi)), float(std::sin(theta) * std::sin(phi)),
						float(std::cos(theta)) };
					const auto half = Pbr::Normalize({ view[0] + light[0], view[1] + light[1], view[2] + light[2] });
					const float nDotL = light[2];
					const float vDotH = std::clamp(Pbr::Dot(view, half), 0.0f, 1.0f);
					const double f = Pbr::DistributionGgx(half[2], alpha) * Pbr::VisibilitySmithGgxCorrelated(nDotV, nDotL, alpha) * nDotL *
						std::sin(theta) * dTheta * dPhi;
					full += f;
					bias += f * std::pow(1.0 - vDotH, 5.0);
				}
			}
			const auto ab = Env::IntegrateBrdf(nDotV, roughness, 4096);
			SWIM_CHECK(ab[0] >= 0.0f && ab[1] >= 0.0f && ab[0] + ab[1] <= 1.0001f);
			SWIM_CHECK(std::abs((ab[0] + ab[1]) - float(full)) < 0.015f);
			SWIM_CHECK(std::abs(ab[1] - float(bias)) < 0.01f);
		}
	}
	// A near-mirror at normal incidence reflects almost everything; albedo falls with roughness.
	const auto mirror = Env::IntegrateBrdf(0.95f, 0.0f, 1024);
	SWIM_CHECK(mirror[0] + mirror[1] > 0.97f);
	float previous = 2.0f;
	for (const float roughness : { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
	{
		const auto ab = Env::IntegrateBrdf(0.5f, roughness, 1024);
		SWIM_CHECK(ab[0] + ab[1] < previous);
		previous = ab[0] + ab[1];
	}
	// Inputs clamp like the direct BRDF.
	SWIM_CHECK((Env::IntegrateBrdf(0.0f, 0.5f, 64) == Env::IntegrateBrdf(1.0e-4f, 0.5f, 64)));
	SWIM_CHECK((Env::IntegrateBrdf(0.5f, 0.0f, 64) == Env::IntegrateBrdf(0.5f, Pbr::MinPerceptualRoughness, 64)));
}

SWIM_TEST("Render.Environment.Math", "PrefilterLodsFollowTheSampleFootprintAndTheMipRoughnessMapping")
{
	SWIM_CHECK_EQUAL(Env::PrefilterMipRoughness(0, 5), 0.0f);
	SWIM_CHECK_EQUAL(Env::PrefilterMipRoughness(2, 5), 0.5f);
	SWIM_CHECK_EQUAL(Env::PrefilterMipRoughness(4, 5), 1.0f);
	SWIM_CHECK_EQUAL(Env::PrefilterMipRoughness(0, 1), 0.0f);
	SWIM_CHECK_EQUAL(Env::PrefilterLodForRoughness(1.0f, 5), 4.0f);
	SWIM_CHECK_EQUAL(Env::PrefilterLodForRoughness(0.5f, 5), 2.0f);
	SWIM_CHECK_EQUAL(Env::PrefilterLodForRoughness(0.0f, 5), Pbr::MinPerceptualRoughness * 4.0f);
	SWIM_CHECK_EQUAL(Env::PrefilterLodForRoughness(3.0f, 5), 4.0f);

	// Rougher lobes and rarer samples read blurrier mips; the result stays in the chain.
	float previous = -1.0f;
	for (const float alpha : { 0.05f, 0.2f, 0.5f, 1.0f })
	{
		const float lod = Env::PrefilterSourceLod(0.9f, alpha, 64, 128, 6);
		SWIM_CHECK(lod >= previous);
		SWIM_CHECK(lod >= 0.0f && lod <= 5.0f);
		previous = lod;
	}
	SWIM_CHECK(Env::PrefilterSourceLod(0.9f, 0.5f, 16, 128, 6) > Env::PrefilterSourceLod(0.9f, 0.5f, 256, 128, 6));
	SWIM_CHECK_EQUAL(Env::PrefilterSourceLod(1.0f, 0.01f, 64, 128, 6), 0.0f); // A sharp peak reads mip 0.
	// Alpha = 1: D = 1 / pi, so the sample solid angle is 4 pi / count exactly.
	const float expected = 0.5f * std::log2((4.0f * Pbr::Pi / 64.0f) / (4.0f * Pbr::Pi / (6.0f * 32.0f * 32.0f))) + 1.0f;
	SWIM_CHECK(std::abs(Env::PrefilterSourceLod(0.5f, 1.0f, 64, 32, 8) - expected) < 1.0e-3f);
}

SWIM_TEST("Render.Environment.Math", "EnvironmentRotationTurnsLookupsAroundY")
{
	const auto rotated = Env::RotateEnvironmentLookup({ 1, 0, 0 }, 0.5f * Pbr::Pi);
	SWIM_CHECK(std::abs(rotated[0]) < 1.0e-6f && std::abs(rotated[1]) < 1.0e-6f && std::abs(rotated[2] - 1.0f) < 1.0e-6f);
	std::mt19937 random(7);
	for (int i = 0; i < 32; ++i)
	{
		const auto d = RandomDirection(random);
		SWIM_CHECK((Env::RotateEnvironmentLookup(d, 0.0f) == d));
		const auto once = Env::RotateEnvironmentLookup(Env::RotateEnvironmentLookup(d, 0.4f), 0.9f);
		const auto both = Env::RotateEnvironmentLookup(d, 1.3f);
		for (int c = 0; c < 3; ++c)
		{
			SWIM_CHECK(std::abs(once[c] - both[c]) < 1.0e-5f);
		}
		SWIM_CHECK(std::abs(Length(both) - 1.0f) < 1.0e-5f);
		SWIM_CHECK_EQUAL(both[1], d[1]);
	}
}

SWIM_TEST("Render.Environment.Math", "ShBasisIsOrthonormalAndIrradianceIsExactUpToBandTwo")
{
	// Orthonormality under the exact cube quadrature.
	const std::uint32_t size = 32;
	std::array<std::array<double, 9>, 9> gram{};
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				const auto basis = Env::ShBasis(Env::CubeTexelDirection(face, x, y, size));
				const double w = Env::CubeTexelSolidAngle(x, y, size);
				for (int i = 0; i < 9; ++i)
				{
					for (int j = 0; j < 9; ++j)
					{
						gram[i][j] += basis[i] * basis[j] * w;
					}
				}
			}
		}
	}
	for (int i = 0; i < 9; ++i)
	{
		for (int j = 0; j < 9; ++j)
		{
			SWIM_CHECK(std::abs(gram[i][j] - (i == j ? 1.0 : 0.0)) < 2.0e-3);
		}
	}
	SWIM_CHECK_EQUAL(Env::ShIrradianceScale(0), 1.0f);
	SWIM_CHECK_EQUAL(Env::ShIrradianceScale(3), 2.0f / 3.0f);
	SWIM_CHECK_EQUAL(Env::ShIrradianceScale(8), 0.25f);

	// Radiance in bands 0..2 convolves exactly: constant L -> L; L = z -> 2/3 n.z;
	// L = 2 + (3z^2 - 1) / 2 -> 2 + (3 n.z^2 - 1) / 8.
	std::mt19937 random(62);
	const auto constant = Env::ProjectIrradianceSh(CubeOf(size,
													   [](const Env::Float3&)
													   {
														   return Env::Float3{ 1.5f, 0.5f, 2.0f };
													   }),
		0);
	const auto linear = Env::ProjectIrradianceSh(CubeOf(size,
													 [](const Env::Float3& d)
													 {
														 return Env::Float3{ d[2], d[2], d[2] };
													 }),
		0);
	const auto quadratic = Env::ProjectIrradianceSh(CubeOf(size,
														[](const Env::Float3& d)
														{
															const float v = 2.0f + 0.5f * (3.0f * d[2] * d[2] - 1.0f);
															return Env::Float3{ v, v, v };
														}),
		0);
	for (int i = 0; i < 48; ++i)
	{
		const auto n = RandomDirection(random);
		const auto c = constant.Evaluate(n);
		SWIM_CHECK(std::abs(c[0] - 1.5f) < 2.0e-3f && std::abs(c[1] - 0.5f) < 2.0e-3f && std::abs(c[2] - 2.0f) < 2.0e-3f);
		SWIM_CHECK(std::abs(linear.Evaluate(n)[0] - std::max(2.0f / 3.0f * n[2], 0.0f)) < 2.0e-3f);
		SWIM_CHECK(std::abs(quadratic.Evaluate(n)[0] - (2.0f + 0.125f * (3.0f * n[2] * n[2] - 1.0f))) < 3.0e-3f);
	}

	// A hemisphere light (L = max(z, 0)) is not band limited: order-2 SH stays within
	// a few percent of the exact irradiance / pi, (1 + n.z) / 2 * ... computed by quadrature.
	const auto cube = CubeOf(size,
		[](const Env::Float3& d)
		{
			const float v = std::max(d[2], 0.0f);
			return Env::Float3{ v, v, v };
		});
	const auto hemisphere = Env::ProjectIrradianceSh(cube, 0);
	for (int i = 0; i < 16; ++i)
	{
		const auto n = RandomDirection(random);
		double exact = 0.0;
		for (std::uint32_t face = 0; face < 6; ++face)
		{
			for (std::uint32_t y = 0; y < size; ++y)
			{
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const auto d = Env::CubeTexelDirection(face, x, y, size);
					exact += cube.Texel(0, face, x, y)[0] * std::max(Env::Dot(d, n), 0.0f) * Env::CubeTexelSolidAngle(x, y, size);
				}
			}
		}
		exact /= Pbr::Pi;
		SWIM_CHECK(std::abs(hemisphere.Evaluate(n)[0] - float(exact)) < 0.035f);
	}
}
