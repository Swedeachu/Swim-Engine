#include "Engine/Systems/Renderer/Lights/LightMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render::Lights
{
	namespace
	{
		bool Finite(const std::array<float, 3>& v)
		{
			return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
		}

		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}
	} // namespace

	std::string ValidateLight(const LightDesc& desc)
	{
		if (desc.Type != LightType::Directional && desc.Type != LightType::Point && desc.Type != LightType::Spot)
		{
			return "unknown light type";
		}
		if (!Finite(desc.Position) || !Finite(desc.Direction) || !Finite(desc.Color) || !std::isfinite(desc.Intensity) ||
			!std::isfinite(desc.Range) || !std::isfinite(desc.InnerConeAngle) || !std::isfinite(desc.OuterConeAngle))
		{
			return "light values must be finite";
		}
		if (desc.Intensity < 0.0f || desc.Color[0] < 0.0f || desc.Color[1] < 0.0f || desc.Color[2] < 0.0f)
		{
			return "light color and intensity must not be negative";
		}
		if (desc.Type != LightType::Point && Dot(desc.Direction, desc.Direction) < 1.0e-12f)
		{
			return "directional and spot lights need a non-zero direction";
		}
		if (IsLocalLight(desc.Type) && !(desc.Range > 0.0f))
		{
			return "point and spot lights need a positive range";
		}
		if (desc.Type == LightType::Spot &&
			!(desc.InnerConeAngle >= 0.0f && desc.InnerConeAngle < desc.OuterConeAngle && desc.OuterConeAngle <= 1.5707964f))
		{
			return "spot cones need 0 <= inner < outer <= pi / 2";
		}
		return {};
	}

	GpuLightRecord EncodeLight(const LightDesc& desc)
	{
		if (const auto error = ValidateLight(desc); !error.empty())
		{
			throw std::invalid_argument("Invalid light: " + error);
		}
		GpuLightRecord record;
		record.Type = static_cast<std::uint32_t>(desc.Type);
		record.Flags = static_cast<std::uint32_t>(desc.Flags);
		record.ShadowIndex = desc.ShadowIndex;
		const auto direction = desc.Type == LightType::Point ? Float3{ 0, -1, 0 } : StandardPbr::Normalize(desc.Direction);
		for (int c = 0; c < 3; ++c)
		{
			record.Position[c] = desc.Type == LightType::Directional ? 0.0f : desc.Position[c];
			record.Direction[c] = direction[c];
			record.Color[c] = desc.Color[c] * desc.Intensity;
		}
		if (IsLocalLight(desc.Type))
		{
			record.Range = desc.Range;
			record.InverseRangeSquared = 1.0f / (desc.Range * desc.Range);
		}
		if (desc.Type == LightType::Spot)
		{
			const float cosOuter = std::cos(desc.OuterConeAngle);
			const float cosInner = std::cos(desc.InnerConeAngle);
			record.SpotScale = 1.0f / std::max(cosInner - cosOuter, MinSpotConeDelta);
			record.SpotOffset = -cosOuter * record.SpotScale;
		}
		return record;
	}

	float RangeAttenuation(float distanceSquared, float inverseRangeSquared)
	{
		const float factor = distanceSquared * inverseRangeSquared;
		const float window = std::clamp(1.0f - factor * factor, 0.0f, 1.0f);
		return window * window / std::max(distanceSquared, MinDistanceSquared);
	}

	float SpotAttenuation(const GpuLightRecord& light, const Float3& toLight)
	{
		const float cd = -(light.Direction[0] * toLight[0] + light.Direction[1] * toLight[1] + light.Direction[2] * toLight[2]);
		const float t = std::clamp(cd * light.SpotScale + light.SpotOffset, 0.0f, 1.0f);
		return t * t;
	}

	LightSample EvaluateLight(const GpuLightRecord& light, const Float3& position)
	{
		LightSample sample;
		if (light.Type == static_cast<std::uint32_t>(LightType::Directional))
		{
			sample.Direction = { -light.Direction[0], -light.Direction[1], -light.Direction[2] };
			sample.Radiance = { light.Color[0], light.Color[1], light.Color[2] };
			return sample;
		}
		const Float3 delta{ light.Position[0] - position[0], light.Position[1] - position[1], light.Position[2] - position[2] };
		const float distanceSquared = Dot(delta, delta);
		const float inverseDistance = 1.0f / std::sqrt(std::max(distanceSquared, 1.0e-12f));
		sample.Direction = { delta[0] * inverseDistance, delta[1] * inverseDistance, delta[2] * inverseDistance };
		const float attenuation = RangeAttenuation(distanceSquared, light.InverseRangeSquared) * SpotAttenuation(light, sample.Direction);
		for (int c = 0; c < 3; ++c)
		{
			sample.Radiance[c] = light.Color[c] * attenuation;
		}
		return sample;
	}

	LightSphere LightBoundingSphere(const GpuLightRecord& light)
	{
		const Float3 position{ light.Position[0], light.Position[1], light.Position[2] };
		LightSphere sphere{ position, light.Range };
		if (light.Type != static_cast<std::uint32_t>(LightType::Spot) || light.SpotScale <= 0.0f)
		{
			return sphere;
		}
		// Spot range is a sphere-capped cone of half-angle a. For a <= pi/4 the sphere
		// through the apex and the rim circle (center h / (2 cos a) along the axis)
		// contains it; for wider cones the sphere around the rim circle does.
		const float cosOuter = std::clamp(-light.SpotOffset / light.SpotScale, 0.0f, 1.0f);
		const float sinOuter = std::sqrt(std::max(1.0f - cosOuter * cosOuter, 0.0f));
		const float h = light.Range;
		float along = 0.0f;
		float radius = h;
		if (cosOuter >= 0.70710678f)
		{
			along = h / (2.0f * cosOuter);
			radius = along;
		}
		else
		{
			along = h * cosOuter;
			radius = h * sinOuter;
		}
		if (radius < sphere.Radius)
		{
			for (int c = 0; c < 3; ++c)
			{
				sphere.Center[c] = position[c] + light.Direction[c] * along;
			}
			sphere.Radius = radius;
		}
		return sphere;
	}

	Float3 ShadeAllLights(std::span<const GpuLightRecord> rows, const GpuLightHeader& header, const StandardPbr::Surface& surface,
		const Float3& normal, const Float3& view, const Float3& position)
	{
		Float3 sum{ 0, 0, 0 };
		const auto add = [&](const GpuLightRecord& light)
		{
			const auto sample = EvaluateLight(light, position);
			const auto brdf = StandardPbr::EvaluateBrdf(surface, normal, view, sample.Direction);
			for (int c = 0; c < 3; ++c)
			{
				sum[c] += brdf[c] * sample.Radiance[c];
			}
		};
		for (std::uint32_t i = 0; i < header.DirectionalCount; ++i)
		{
			add(rows[i]);
		}
		for (std::uint32_t i = 0; i < header.LocalCount; ++i)
		{
			add(rows[header.FirstLocalRow + i]);
		}
		return sum;
	}
} // namespace Swim::Render::Lights
