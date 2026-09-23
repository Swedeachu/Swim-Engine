#include "Engine/Systems/Renderer/Environment/EnvironmentMath.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render::Environment
{
	Float3 Normalize(const Float3& value)
	{
		const float length = std::sqrt(Dot(value, value));
		return length > 0.0f ? Float3{ value[0] / length, value[1] / length, value[2] / length } : value;
	}

	float Dot(const Float3& a, const Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	Float3 Cross(const Float3& a, const Float3& b)
	{
		return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	}

	Float3 CubeFaceDirection(std::uint32_t face, float s, float t)
	{
		switch (face)
		{
		case 0:
			return { 1.0f, -t, -s };
		case 1:
			return { -1.0f, -t, s };
		case 2:
			return { s, 1.0f, t };
		case 3:
			return { s, -1.0f, -t };
		case 4:
			return { s, -t, 1.0f };
		case 5:
			return { -s, -t, -1.0f };
		default:
			throw std::out_of_range("Cube face must be 0..5");
		}
	}

	Float3 CubeTexelDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size)
	{
		const float s = 2.0f * (float(x) + 0.5f) / float(size) - 1.0f;
		const float t = 2.0f * (float(y) + 0.5f) / float(size) - 1.0f;
		return Normalize(CubeFaceDirection(face, s, t));
	}

	CubeCoordinate DirectionToCube(const Float3& d)
	{
		const float ax = std::abs(d[0]);
		const float ay = std::abs(d[1]);
		const float az = std::abs(d[2]);
		if (ax >= ay && ax >= az)
		{
			return d[0] >= 0.0f ? CubeCoordinate{ 0, -d[2] / ax, -d[1] / ax } : CubeCoordinate{ 1, d[2] / ax, -d[1] / ax };
		}
		if (ay >= az)
		{
			return d[1] >= 0.0f ? CubeCoordinate{ 2, d[0] / ay, d[2] / ay } : CubeCoordinate{ 3, d[0] / ay, -d[2] / ay };
		}
		return d[2] >= 0.0f ? CubeCoordinate{ 4, d[0] / az, -d[1] / az } : CubeCoordinate{ 5, -d[0] / az, -d[1] / az };
	}

	namespace
	{
		// Solid angle of the face region [-1, x] x [-1, y] up to a constant.
		float AreaElement(float x, float y)
		{
			return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
		}
	} // namespace

	float CubeTexelSolidAngle(std::uint32_t x, std::uint32_t y, std::uint32_t size)
	{
		const float step = 2.0f / float(size);
		const float x0 = float(x) * step - 1.0f;
		const float y0 = float(y) * step - 1.0f;
		const float x1 = x0 + step;
		const float y1 = y0 + step;
		return AreaElement(x0, y0) - AreaElement(x0, y1) - AreaElement(x1, y0) + AreaElement(x1, y1);
	}

	Float2 Hammersley(std::uint32_t index, std::uint32_t count)
	{
		std::uint32_t bits = index;
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return { float(index) / float(count), float(bits) * 2.3283064365386963e-10f };
	}

	Float3 ImportanceSampleGgx(const Float2& xi, float alpha)
	{
		const float phi = 2.0f * Pi * xi[0];
		const float cosTheta = std::sqrt((1.0f - xi[1]) / (1.0f + (alpha * alpha - 1.0f) * xi[1]));
		const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
		return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
	}

	Float3 TangentToWorld(const Float3& value, const Float3& normal)
	{
		const Float3 up = std::abs(normal[2]) < 0.999f ? Float3{ 0, 0, 1 } : Float3{ 1, 0, 0 };
		const auto tangent = Normalize(Cross(up, normal));
		const auto bitangent = Cross(normal, tangent);
		Float3 result;
		for (int c = 0; c < 3; ++c)
		{
			result[c] = tangent[c] * value[0] + bitangent[c] * value[1] + normal[c] * value[2];
		}
		return result;
	}

	Float2 IntegrateBrdf(float nDotVInput, float perceptualRoughness, std::uint32_t sampleCount)
	{
		const float nDotV = std::clamp(nDotVInput, 1.0e-4f, 1.0f);
		const float roughness = std::clamp(perceptualRoughness, StandardPbr::MinPerceptualRoughness, 1.0f);
		const float alpha = roughness * roughness;
		const Float3 view{ std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV };
		float a = 0.0f;
		float b = 0.0f;
		for (std::uint32_t i = 0; i < sampleCount; ++i)
		{
			const auto half = ImportanceSampleGgx(Hammersley(i, sampleCount), alpha);
			const float vDotH = Dot(view, half);
			const Float3 light{ 2.0f * vDotH * half[0] - view[0], 2.0f * vDotH * half[1] - view[1], 2.0f * vDotH * half[2] - view[2] };
			const float nDotL = light[2];
			const float nDotH = half[2];
			if (nDotL > 0.0f && nDotH > 0.0f && vDotH > 0.0f)
			{
				// f * N.L / pdf with f = D * Vis * F and pdf = D * N.H / (4 V.H).
				const float visibility = StandardPbr::VisibilitySmithGgxCorrelated(nDotV, nDotL, alpha);
				const float weight = 4.0f * visibility * nDotL * vDotH / nDotH;
				const float fresnel = std::pow(1.0f - vDotH, 5.0f);
				a += (1.0f - fresnel) * weight;
				b += fresnel * weight;
			}
		}
		return { a / float(sampleCount), b / float(sampleCount) };
	}

	float PrefilterSourceLod(float nDotH, float alpha, std::uint32_t sampleCount, std::uint32_t sourceSize, std::uint32_t sourceMipCount)
	{
		// With N = V the light pdf is D(h) * N.H / (4 V.H) = D(h) / 4.
		const float pdf = StandardPbr::DistributionGgx(nDotH, alpha) * 0.25f;
		const float sampleSolidAngle = 1.0f / (float(sampleCount) * pdf + 1.0e-6f);
		const float texelSolidAngle = 4.0f * Pi / (6.0f * float(sourceSize) * float(sourceSize));
		const float lod = 0.5f * std::log2(sampleSolidAngle / texelSolidAngle) + 1.0f;
		return std::clamp(lod, 0.0f, float(sourceMipCount - 1));
	}

	float PrefilterMipRoughness(std::uint32_t mip, std::uint32_t mipCount)
	{
		return mipCount > 1 ? float(mip) / float(mipCount - 1) : 0.0f;
	}

	float PrefilterLodForRoughness(float perceptualRoughness, std::uint32_t mipCount)
	{
		return std::clamp(perceptualRoughness, StandardPbr::MinPerceptualRoughness, 1.0f) * float(mipCount - 1);
	}

	Float3 RotateEnvironmentLookup(const Float3& d, float rotation)
	{
		const float c = std::cos(rotation);
		const float s = std::sin(rotation);
		return { c * d[0] - s * d[2], d[1], s * d[0] + c * d[2] };
	}

	std::array<float, ShCoefficientCount> ShBasis(const Float3& d)
	{
		const float x = d[0];
		const float y = d[1];
		const float z = d[2];
		return { 0.282094792f, 0.488602512f * y, 0.488602512f * z, 0.488602512f * x, 1.092548431f * x * y, 1.092548431f * y * z,
			0.315391565f * (3.0f * z * z - 1.0f), 1.092548431f * x * z, 0.546274215f * (x * x - y * y) };
	}

	float ShIrradianceScale(std::uint32_t coefficient)
	{
		// A_l / pi with A_0 = pi, A_1 = 2 pi / 3, A_2 = pi / 4 (Ramamoorthi & Hanrahan 2001).
		return coefficient == 0 ? 1.0f : coefficient < 4 ? 2.0f / 3.0f : 0.25f;
	}

	Float3 IrradianceSh::Evaluate(const Float3& normal) const
	{
		const auto basis = ShBasis(normal);
		Float3 result{ 0, 0, 0 };
		for (std::uint32_t i = 0; i < ShCoefficientCount; ++i)
		{
			for (int c = 0; c < 3; ++c)
			{
				result[c] += Coefficients[i][c] * basis[i];
			}
		}
		for (auto& value : result)
		{
			value = std::max(value, 0.0f);
		}
		return result;
	}
} // namespace Swim::Render::Environment
