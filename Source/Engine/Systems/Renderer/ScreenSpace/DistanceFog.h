#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Distance fog curves shared by the scene fog (ScreenSpace FogSettings) and surfaces that fog
// themselves (OceanSurface, WaveWater, Water/WaterShading.slang), so water fades exactly like
// the ground around it. Mirrored by Shaders/Slang/ScreenSpace/DistanceFog.slang.
namespace Engine
{

	enum class FogCurve : std::uint32_t
	{
		Exponential = 0, // 1 - e^(-Density * (d - Start)): thickens from Start onward, never fully opaque.
		Linear = 1,		 // 0 at Start to 1 at End.
		Smooth = 2,		 // Smoothstep from Start to End.
		Power = 3,		 // ((d - Start) / (End - Start))^Exponent: stays clear until close to End (light far-edge fog).
	};

	// 0..1 fog amount at a distance, before MaxOpacity.
	inline float DistanceFogShape(FogCurve curve, float start, float end, float density, float exponent, float distance)
	{
		if (!(distance > start))
		{
			return 0.0f;
		}

		if (curve == FogCurve::Exponential)
		{
			return 1.0f - std::exp(-std::max(density, 0.0f) * (distance - start));
		}

		const float t = std::clamp((distance - start) / std::max(end - start, 1.0e-3f), 0.0f, 1.0f);

		switch (curve)
		{
		case FogCurve::Linear:
			return t;
		case FogCurve::Smooth:
			return t * t * (3.0f - 2.0f * t);
		default:
			return std::pow(t, std::max(exponent, 1.0e-3f));
		}
	}

	// Fog a surface applies to itself at its distance from the camera.
	struct DistanceFogSettings
	{
		bool Enabled = false;
		FogCurve Curve = FogCurve::Smooth;
		std::array<float, 3> Color{ 0.5f, 0.6f, 0.7f }; // Linear radiance the surface fades into.
		float StartDistance = 0.0f;
		float EndDistance = 1000.0f;
		float Density = 0.002f;	 // Exponential only (1/m).
		float Exponent = 2.0f;	 // Power only.
		float MaxOpacity = 1.0f; // The most fog ever covers (0..1).

		float Amount(float distance) const
		{
			return Enabled
				? std::clamp(MaxOpacity, 0.0f, 1.0f) * DistanceFogShape(Curve, StartDistance, EndDistance, Density, Exponent, distance)
				: 0.0f;
		}
	};

	// DistanceFog in DistanceFog.slang.
	struct GpuDistanceFog
	{
		float ColorOpacity[4] = {}; // rgb: colour, w: max opacity (0: off).
		float Range[4] = {};		// Start, end, density, exponent.
		std::uint32_t Curve = 0;
		std::uint32_t Pad[3] = {};
	};

	static_assert(sizeof(GpuDistanceFog) == 48);

	inline GpuDistanceFog PackDistanceFog(const DistanceFogSettings& fog)
	{
		GpuDistanceFog gpu;

		for (int c = 0; c < 3; ++c)
		{
			gpu.ColorOpacity[c] = std::max(fog.Color[c], 0.0f);
		}

		gpu.ColorOpacity[3] = fog.Enabled ? std::clamp(fog.MaxOpacity, 0.0f, 1.0f) : 0.0f;
		gpu.Range[0] = std::max(fog.StartDistance, 0.0f);
		gpu.Range[1] = std::max(fog.EndDistance, gpu.Range[0]);
		gpu.Range[2] = std::max(fog.Density, 0.0f);
		gpu.Range[3] = std::max(fog.Exponent, 1.0e-3f);
		gpu.Curve = static_cast<std::uint32_t>(fog.Curve);
		return gpu;
	}

} // namespace Engine
