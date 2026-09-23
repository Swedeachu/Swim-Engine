#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::Render::Environment
{
	CubeImage BuildSkyCube(const ProceduralSky& sky, std::uint32_t size)
	{
		CubeImage cube(size, EnvironmentSourceMipCount(size));
		for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
		{
			for (std::uint32_t y = 0; y < size; ++y)
			{
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const auto radiance = sky.Evaluate(CubeTexelDirection(face, x, y, size));
					cube.Texel(0, face, x, y) = { radiance[0], radiance[1], radiance[2], 1.0f };
				}
			}
		}
		cube.GenerateMips();
		return cube;
	}

	Float3 PrefilterDirection(
		const CubeImage& source, const Float3& direction, float perceptualRoughness, std::uint32_t sampleCount, CubeSampling sampling)
	{
		const auto sample = [&](const Float3& d, float lod)
		{
			const auto texel = sampling == CubeSampling::Nearest ? source.SampleNearest(d, lod) : source.SampleTrilinear(d, lod);
			return Float3{ texel[0], texel[1], texel[2] };
		};
		const auto normal = Normalize(direction);
		if (perceptualRoughness <= 0.0f || sampleCount == 0)
		{
			return sample(normal, 0.0f);
		}
		const float roughness = std::clamp(perceptualRoughness, StandardPbr::MinPerceptualRoughness, 1.0f);
		const float alpha = roughness * roughness;
		Float3 sum{ 0, 0, 0 };
		float weight = 0.0f;
		for (std::uint32_t i = 0; i < sampleCount; ++i)
		{
			const auto local = ImportanceSampleGgx(Hammersley(i, sampleCount), alpha);
			const auto half = TangentToWorld(local, normal);
			const float nDotH = local[2];
			const Float3 light{ 2.0f * nDotH * half[0] - normal[0], 2.0f * nDotH * half[1] - normal[1],
				2.0f * nDotH * half[2] - normal[2] };
			const float nDotL = Dot(normal, light);
			if (nDotL > 0.0f)
			{
				const float lod = PrefilterSourceLod(nDotH, alpha, sampleCount, source.GetSize(), source.GetMipCount());
				const auto radiance = sample(light, lod);
				for (int c = 0; c < 3; ++c)
				{
					sum[c] += radiance[c] * nDotL;
				}
				weight += nDotL;
			}
		}
		if (weight <= 0.0f)
		{
			return sample(normal, 0.0f);
		}
		return { sum[0] / weight, sum[1] / weight, sum[2] / weight };
	}

	CubeImage BuildPrefilteredCube(
		const CubeImage& source, std::uint32_t size, std::uint32_t mipCount, std::uint32_t sampleCount, CubeSampling sampling)
	{
		CubeImage result(size, mipCount);
		for (std::uint32_t mip = 0; mip < mipCount; ++mip)
		{
			const std::uint32_t mipSize = result.GetMipSize(mip);
			const float roughness = PrefilterMipRoughness(mip, mipCount);
			for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
			{
				for (std::uint32_t y = 0; y < mipSize; ++y)
				{
					for (std::uint32_t x = 0; x < mipSize; ++x)
					{
						const auto value =
							PrefilterDirection(source, CubeTexelDirection(face, x, y, mipSize), roughness, sampleCount, sampling);
						result.Texel(mip, face, x, y) = { value[0], value[1], value[2], 1.0f };
					}
				}
			}
		}
		return result;
	}

	IrradianceSh ProjectIrradianceSh(const CubeImage& cube, std::uint32_t mip)
	{
		IrradianceSh sh;
		const std::uint32_t size = cube.GetMipSize(mip);
		for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
		{
			for (std::uint32_t y = 0; y < size; ++y)
			{
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const auto basis = ShBasis(CubeTexelDirection(face, x, y, size));
					const float solidAngle = CubeTexelSolidAngle(x, y, size);
					const auto& radiance = cube.Texel(mip, face, x, y);
					for (std::uint32_t i = 0; i < ShCoefficientCount; ++i)
					{
						for (int c = 0; c < 3; ++c)
						{
							sh.Coefficients[i][c] += radiance[c] * basis[i] * solidAngle;
						}
					}
				}
			}
		}
		for (std::uint32_t i = 0; i < ShCoefficientCount; ++i)
		{
			for (auto& value : sh.Coefficients[i])
			{
				value *= ShIrradianceScale(i);
			}
		}
		return sh;
	}

	Image2D BuildBrdfLut(std::uint32_t size, std::uint32_t sampleCount)
	{
		Image2D lut{ size, size, std::vector<Float4>(std::size_t(size) * size) };
		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				const auto ab = IntegrateBrdf((float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size), sampleCount);
				lut.Texels[std::size_t(y) * size + x] = { ab[0], ab[1], 0.0f, 1.0f };
			}
		}
		return lut;
	}

	EnvironmentProbe::EnvironmentProbe(IrradianceSh irradianceInput, CubeImage prefilteredInput, Image2D brdfLutInput)
		: irradiance(irradianceInput), prefiltered(std::move(prefilteredInput)), brdfLut(std::move(brdfLutInput))
	{
		if (prefiltered.GetMipCount() == 0 || brdfLut.Width == 0 || brdfLut.Height == 0 ||
			brdfLut.Texels.size() != std::size_t(brdfLut.Width) * brdfLut.Height)
		{
			throw std::invalid_argument("EnvironmentProbe needs a prefiltered cube and a BRDF LUT");
		}
	}

	StandardPbr::EnvironmentTerms EnvironmentProbe::Lookup(
		const StandardPbr::ResolvedSurface& surface, const Float3& view, const EnvironmentLighting& lighting) const
	{
		const auto reflected = StandardPbr::Reflect(view, surface.Normal);
		const float roughness = std::clamp(surface.PerceptualRoughness, StandardPbr::MinPerceptualRoughness, 1.0f);
		const float nDotV = std::clamp(Dot(surface.Normal, view), 1.0e-4f, 1.0f);
		const auto diffuse = irradiance.Evaluate(RotateEnvironmentLookup(surface.Normal, lighting.Rotation));
		const auto specular = prefiltered.SampleTrilinear(
			RotateEnvironmentLookup(reflected, lighting.Rotation), PrefilterLodForRoughness(roughness, prefiltered.GetMipCount()));
		const auto ab = brdfLut.SampleBilinear(nDotV, roughness);
		StandardPbr::EnvironmentTerms terms;
		for (int c = 0; c < 3; ++c)
		{
			terms.Irradiance[c] = diffuse[c] * lighting.Intensity;
			terms.Prefiltered[c] = specular[c] * lighting.Intensity;
		}
		terms.BrdfScale = ab[0];
		terms.BrdfBias = ab[1];
		return terms;
	}
} // namespace Swim::Render::Environment
