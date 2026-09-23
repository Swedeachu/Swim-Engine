#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <random>

using namespace Swim;
using namespace Swim::Render;
namespace Pbr = Swim::Render::StandardPbr;

namespace
{
	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	Pbr::Float3 Direction(float theta, float phi)
	{
		return { std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) };
	}

	// Hemispherical integral of BRDF * cos for a fixed view (directional albedo).
	Pbr::Float3 DirectionalAlbedo(const Pbr::Surface& surface, const Pbr::Float3& view, int steps = 256)
	{
		Pbr::Float3 sum{ 0, 0, 0 };
		const float dTheta = 0.5f * Pbr::Pi / float(steps);
		const float dPhi = 2.0f * Pbr::Pi / float(steps);
		for (int i = 0; i < steps; ++i)
		{
			const float theta = (float(i) + 0.5f) * dTheta;
			for (int j = 0; j < steps; ++j)
			{
				const float phi = (float(j) + 0.5f) * dPhi;
				const auto value = Pbr::EvaluateBrdf(surface, { 0, 0, 1 }, view, Direction(theta, phi));
				for (int c = 0; c < 3; ++c)
				{
					sum[c] += value[c] * std::sin(theta) * dTheta * dPhi;
				}
			}
		}
		return sum;
	}
} // namespace

SWIM_TEST("Render.StandardPbr", "SrgbTransferFunctionsRoundTripAndMatchTheStandard")
{
	SWIM_CHECK(Near(Pbr::SrgbToLinear(0.0f), 0.0f, 1e-7f));
	SWIM_CHECK(Near(Pbr::SrgbToLinear(1.0f), 1.0f, 1e-6f));
	SWIM_CHECK(Near(Pbr::SrgbToLinear(0.5f), 0.2140411f, 1e-6f));
	SWIM_CHECK(Near(Pbr::SrgbToLinear(0.04045f), 0.04045f / 12.92f, 1e-7f)); // Linear segment.
	for (int i = 0; i <= 255; ++i)
	{
		const float encoded = float(i) / 255.0f;
		SWIM_CHECK(Near(Pbr::LinearToSrgb(Pbr::SrgbToLinear(encoded)), encoded, 2e-6f));
	}
}

SWIM_TEST("Render.StandardPbr", "GgxIsNormalizedAndFresnelHitsItsLimits")
{
	// Projected-area normalization: integral of D(h) * (n.h) over the hemisphere is 1.
	for (const float alpha : { 0.1f, 0.3f, 0.6f, 1.0f })
	{
		double integral = 0.0;
		const int steps = 20000;
		const double dTheta = 0.5 * Pbr::Pi / steps;
		for (int i = 0; i < steps; ++i)
		{
			const double theta = (i + 0.5) * dTheta;
			integral += Pbr::DistributionGgx(float(std::cos(theta)), alpha) * std::cos(theta) * std::sin(theta) * dTheta * 2.0 * Pbr::Pi;
		}
		SWIM_CHECK(std::abs(integral - 1.0) < 2e-3);
	}
	const auto f = Pbr::FresnelSchlick({ 0.04f, 0.5f, 1.0f }, 1.0f);
	SWIM_CHECK(Near(f[0], 0.04f, 1e-7f) && Near(f[1], 0.5f, 1e-7f) && Near(f[2], 1.0f, 1e-7f));
	const auto grazing = Pbr::FresnelSchlick({ 0.04f, 0.04f, 0.04f }, 0.0f);
	SWIM_CHECK(Near(grazing[0], 1.0f, 1e-6f));
	// The correlated Smith term at normal incidence is 1 / (4 n.l n.v) scaled by masking (<= 0.25).
	SWIM_CHECK(Near(Pbr::VisibilitySmithGgxCorrelated(1.0f, 1.0f, 0.5f), 0.25f, 1e-6f));
}

SWIM_TEST("Render.StandardPbr", "BrdfIsReciprocalBoundedAndZeroBelowTheHorizon")
{
	std::mt19937 random(3);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (int i = 0; i < 200; ++i)
	{
		Pbr::Surface surface{ { unit(random), unit(random), unit(random) }, unit(random), unit(random) };
		const auto view = Direction(unit(random) * 1.5f, unit(random) * 6.28f);
		const auto light = Direction(unit(random) * 1.5f, unit(random) * 6.28f);
		const auto forward = Pbr::EvaluateBrdf(surface, { 0, 0, 1 }, view, light);
		const auto reverse = Pbr::EvaluateBrdf(surface, { 0, 0, 1 }, light, view);
		for (int c = 0; c < 3; ++c)
		{
			// f(v, l) cos(l) / cos(l) == f(l, v) cos(v) / cos(v).
			SWIM_CHECK(Near(forward[c] / light[2], reverse[c] / view[2], 1e-3f * std::max(1.0f, forward[c] / light[2])));
		}
	}
	const auto below = Pbr::EvaluateBrdf({}, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0.6f, -0.8f });
	SWIM_CHECK(below[0] == 0.0f && below[1] == 0.0f && below[2] == 0.0f);

	// Energy: directional albedo never exceeds 1 (no lighting gain) for white surfaces.
	for (const float roughness : { 0.1f, 0.5f, 1.0f })
	{
		for (const float metallic : { 0.0f, 1.0f })
		{
			const auto albedo = DirectionalAlbedo({ { 1, 1, 1 }, metallic, roughness }, Direction(0.6f, 0.3f));
			SWIM_CHECK(albedo[0] <= 1.02f);
			SWIM_CHECK(albedo[0] > 0.3f);
		}
	}
	// Metals have no diffuse lobe: a black metal reflects only its (black) F0 plus Fresnel.
	const auto metal = Pbr::EvaluateBrdf({ { 0, 0, 0 }, 1.0f, 1.0f }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 });
	SWIM_CHECK(Near(metal[0], 0.0f, 1e-6f));
	// Smoother surfaces have a sharper, higher mirror peak.
	const auto rough = Pbr::EvaluateBrdf({ { 1, 1, 1 }, 1.0f, 0.8f }, { 0, 0, 1 }, Direction(0.5f, 0), Direction(0.5f, Pbr::Pi));
	const auto smooth = Pbr::EvaluateBrdf({ { 1, 1, 1 }, 1.0f, 0.2f }, { 0, 0, 1 }, Direction(0.5f, 0), Direction(0.5f, Pbr::Pi));
	SWIM_CHECK(smooth[0] > rough[0] * 4.0f);
	// Perceptual roughness 0 is clamped, keeping the peak finite.
	const auto mirror = Pbr::EvaluateBrdf({ { 1, 1, 1 }, 1.0f, 0.0f }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 });
	SWIM_CHECK(std::isfinite(mirror[0]) && mirror[0] > 100.0f);
}

SWIM_TEST("Render.StandardPbr", "ShadeAppliesTexturesNormalsOcclusionEmissionAndAlphaModes")
{
	Pbr::Parameters parameters;
	parameters.BaseColorFactor = { 0.8f, 0.4f, 0.2f, 1.0f };
	parameters.MetallicFactor = 0.0f;
	parameters.RoughnessFactor = 0.5f;
	Pbr::Lighting lighting;
	lighting.LightDirection = Pbr::Normalize({ 0.3f, 0.2f, 1.0f });
	lighting.LightRadiance = { 2, 2, 2 };
	lighting.Ambient = { 0.1f, 0.1f, 0.1f };
	const Pbr::Frame frame;
	const Pbr::Texels plain;

	const auto base = Pbr::Shade(parameters, plain, frame, lighting);
	SWIM_REQUIRE(base.has_value());
	const auto brdf = Pbr::EvaluateBrdf({ { 0.8f, 0.4f, 0.2f }, 0.0f, 0.5f }, { 0, 0, 1 }, lighting.View, lighting.LightDirection);
	SWIM_CHECK(Near((*base)[0], brdf[0] * 2.0f + 0.1f * 0.8f, 1e-6f));
	SWIM_CHECK_EQUAL((*base)[3], 1.0f);

	// Occlusion only scales the ambient term; emission adds factor x texel.
	auto texels = plain;
	texels.Occlusion = 0.0f;
	parameters.OcclusionStrength = 0.5f;
	parameters.EmissiveFactor = { 1, 0, 0 };
	texels.Emissive = { 0.5f, 1, 1 };
	const auto lit = Pbr::Shade(parameters, texels, frame, lighting);
	SWIM_CHECK(Near((*lit)[0], brdf[0] * 2.0f + 0.1f * 0.8f * 0.5f + 0.5f, 1e-6f));
	SWIM_CHECK(Near((*lit)[1], brdf[1] * 2.0f + 0.1f * 0.4f * 0.5f, 1e-6f));

	// Texture channels: G = roughness, B = metallic.
	texels = plain;
	texels.MetallicRoughness = { 0.0f, 0.25f, 1.0f };
	parameters = {};
	const auto metal = Pbr::Shade(parameters, texels, frame, lighting);
	const auto metalBrdf = Pbr::EvaluateBrdf({ { 1, 1, 1 }, 1.0f, 0.25f }, { 0, 0, 1 }, lighting.View, lighting.LightDirection);
	SWIM_CHECK(Near((*metal)[0], metalBrdf[0] * 2.0f + 0.1f, 1e-5f));

	// A tangent-space normal tilts the shading normal in the tangent frame.
	texels = plain;
	texels.TangentNormal = Pbr::Float3{ 0.6f, 0.0f, 0.8f };
	const auto tilted = Pbr::Shade(parameters, texels, frame, lighting);
	const auto tiltedBrdf = Pbr::EvaluateBrdf({ { 1, 1, 1 }, 1.0f, 1.0f }, { 0.6f, 0, 0.8f }, lighting.View, lighting.LightDirection);
	SWIM_CHECK(Near((*tilted)[0], tiltedBrdf[0] * 2.0f + 0.1f, 1e-5f));
	parameters.NormalScale = 0.0f; // Scale 0 flattens the map.
	const auto flattened = Pbr::Shade(parameters, texels, frame, lighting);
	SWIM_CHECK(Near((*flattened)[0], (*Pbr::Shade(parameters, plain, frame, lighting))[0], 1e-6f));

	// Alpha mask discards below the cutoff; blend/opaque keep the alpha.
	parameters = {};
	parameters.BaseColorFactor[3] = 0.4f;
	SWIM_CHECK(Pbr::Shade(parameters, plain, frame, lighting).has_value());
	parameters.Flags = Pbr::FlagAlphaMask;
	SWIM_CHECK(!Pbr::Shade(parameters, plain, frame, lighting).has_value());
	parameters.AlphaCutoff = 0.3f;
	SWIM_CHECK(Pbr::Shade(parameters, plain, frame, lighting).has_value());

	// Double-sided back faces shade with the flipped normal; single-sided ones do not flip.
	parameters = {};
	Pbr::Frame back;
	back.FrontFacing = false;
	back.Normal = { 0, 0, -1 };
	const auto singleSided = Pbr::Shade(parameters, plain, back, lighting);
	SWIM_CHECK(Near((*singleSided)[0], 0.1f, 1e-6f)); // Light is behind: ambient only.
	parameters.Flags = Pbr::FlagDoubleSided;
	const auto doubleSided = Pbr::Shade(parameters, plain, back, lighting);
	SWIM_CHECK((*doubleSided)[0] > 0.2f);
}

SWIM_TEST("Render.Materials", "StandardTemplateCarriesGltfDefaultsAndDecodes")
{
	const auto materialTemplate = CreateStandardMaterialTemplate();
	SWIM_CHECK_EQUAL(materialTemplate->GetRecordSize(), StandardMaterialRecordSize);
	MaterialInstance instance(materialTemplate);
	auto parameters = ReadStandardParameters(instance);
	SWIM_CHECK((parameters.BaseColorFactor == std::array<float, 4>{ 1, 1, 1, 1 }));
	SWIM_CHECK(parameters.MetallicFactor == 1.0f && parameters.RoughnessFactor == 1.0f && parameters.NormalScale == 1.0f);
	SWIM_CHECK(parameters.OcclusionStrength == 1.0f && parameters.AlphaCutoff == 0.5f && parameters.Flags == 0u);
	SWIM_CHECK((parameters.EmissiveFactor == Pbr::Float3{ 0, 0, 0 }));
	const auto textures = ReadStandardTextures(instance);
	SWIM_CHECK(textures.BaseColor == 0u && textures.Normal == 0u && textures.Sampler == 0u);

	instance.SetVector("BaseColorFactor", std::array<float, 4>{ 0.5f, 0.25f, 0.125f, 1.0f });
	instance.SetFloat("RoughnessFactor", 0.3f);
	instance.SetUint("Flags", Pbr::FlagAlphaMask);
	instance.SetTexture("NormalTexture", 7);
	instance.SetSampler("MaterialSampler", 2);
	parameters = ReadStandardParameters(instance);
	SWIM_CHECK_EQUAL(parameters.BaseColorFactor[1], 0.25f);
	SWIM_CHECK_EQUAL(parameters.RoughnessFactor, 0.3f);
	SWIM_CHECK_EQUAL(parameters.Flags, Pbr::FlagAlphaMask);
	SWIM_CHECK_EQUAL(ReadStandardTextures(instance).Normal, 7u);
	SWIM_CHECK_EQUAL(ReadStandardTextures(instance).Sampler, 2u);

	// Another layout is rejected.
	MaterialTemplateDesc other;
	other.Name = "Other";
	other.RecordSize = 16;
	other.Parameters = { { "Color", MaterialParameterType::Float4, 0 } };
	MaterialInstance foreign(std::make_shared<const MaterialTemplate>(other));
	SWIM_CHECK_THROWS(ReadStandardParameters(foreign), std::invalid_argument);
}
