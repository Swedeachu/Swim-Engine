#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

#include <algorithm>
#include <cmath>

namespace Swim::Render::StandardPbr
{
	float Dot(const Float3& a, const Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	Float3 Normalize(const Float3& value)
	{
		const float length = std::sqrt(Dot(value, value));
		return length > 0.0f ? Float3{ value[0] / length, value[1] / length, value[2] / length } : value;
	}

	float SrgbToLinear(float encoded)
	{
		return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
	}

	float LinearToSrgb(float linear)
	{
		return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	float DistributionGgx(float nDotH, float alpha)
	{
		const float a2 = alpha * alpha;
		const float f = (nDotH * a2 - nDotH) * nDotH + 1.0f;
		return a2 / (Pi * f * f);
	}

	float VisibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
	{
		const float a2 = alpha * alpha;
		const float ggxV = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
		const float ggxL = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
		return 0.5f / (ggxV + ggxL);
	}

	Float3 FresnelSchlick(const Float3& f0, float vDotH)
	{
		const float f = std::pow(1.0f - vDotH, 5.0f);
		return { f0[0] + (1.0f - f0[0]) * f, f0[1] + (1.0f - f0[1]) * f, f0[2] + (1.0f - f0[2]) * f };
	}

	Float3 EvaluateBrdf(const Surface& surface, const Float3& normal, const Float3& view, const Float3& light)
	{
		const float nDotL = Dot(normal, light);
		if (nDotL <= 0.0f)
		{
			return { 0, 0, 0 };
		}
		const float nDotV = std::max(Dot(normal, view), 1.0e-4f);
		const auto half = Normalize({ view[0] + light[0], view[1] + light[1], view[2] + light[2] });
		const float nDotH = std::clamp(Dot(normal, half), 0.0f, 1.0f);
		const float vDotH = std::clamp(Dot(view, half), 0.0f, 1.0f);
		const float roughness = std::clamp(surface.PerceptualRoughness, MinPerceptualRoughness, 1.0f);
		const float alpha = roughness * roughness;
		const float metallic = std::clamp(surface.Metallic, 0.0f, 1.0f);
		Float3 f0;
		Float3 diffuseColor;
		for (int c = 0; c < 3; ++c)
		{
			f0[c] = DielectricF0 + (surface.BaseColor[c] - DielectricF0) * metallic;
			diffuseColor[c] = surface.BaseColor[c] * (1.0f - metallic);
		}
		const auto fresnel = FresnelSchlick(f0, vDotH);
		const float dv = DistributionGgx(nDotH, alpha) * VisibilitySmithGgxCorrelated(nDotV, nDotL, alpha);
		Float3 result;
		for (int c = 0; c < 3; ++c)
		{
			const float diffuse = (1.0f - fresnel[c]) * diffuseColor[c] / Pi;
			result[c] = (diffuse + dv * fresnel[c]) * nDotL;
		}
		return result;
	}

	std::optional<std::array<float, 4>> Shade(
		const Parameters& parameters, const Texels& texels, const Frame& frame, const Lighting& lighting)
	{
		const float alpha = parameters.BaseColorFactor[3] * texels.BaseColor[3];
		if ((parameters.Flags & FlagAlphaMask) != 0 && alpha < parameters.AlphaCutoff)
		{
			return std::nullopt;
		}
		const bool flip = !frame.FrontFacing && (parameters.Flags & FlagDoubleSided) != 0;
		const float sign = flip ? -1.0f : 1.0f;
		Float3 normal{ frame.Normal[0] * sign, frame.Normal[1] * sign, frame.Normal[2] * sign };
		if (texels.TangentNormal)
		{
			const auto& t = *texels.TangentNormal;
			const auto n = Normalize({ t[0] * parameters.NormalScale, t[1] * parameters.NormalScale, t[2] });
			for (int c = 0; c < 3; ++c)
			{
				normal[c] = frame.Tangent[c] * n[0] + frame.Bitangent[c] * n[1] + normal[c] * n[2];
			}
			normal = Normalize(normal);
		}
		Surface surface;
		for (int c = 0; c < 3; ++c)
		{
			surface.BaseColor[c] = parameters.BaseColorFactor[c] * texels.BaseColor[c];
		}
		surface.Metallic = parameters.MetallicFactor * texels.MetallicRoughness[2];
		surface.PerceptualRoughness = parameters.RoughnessFactor * texels.MetallicRoughness[1];
		const float occlusion = 1.0f + parameters.OcclusionStrength * (texels.Occlusion - 1.0f);
		const auto brdf = EvaluateBrdf(surface, normal, lighting.View, lighting.LightDirection);
		std::array<float, 4> color{ 0, 0, 0, alpha };
		for (int c = 0; c < 3; ++c)
		{
			color[c] = brdf[c] * lighting.LightRadiance[c] + lighting.Ambient[c] * surface.BaseColor[c] * occlusion +
				parameters.EmissiveFactor[c] * texels.Emissive[c];
		}
		return color;
	}
} // namespace Swim::Render::StandardPbr
