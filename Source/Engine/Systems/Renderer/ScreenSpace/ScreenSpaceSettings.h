#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	inline constexpr std::uint32_t MaxAoSlices = 4;
	inline constexpr std::uint32_t MaxAoSteps = 8;

	// Ground-truth-based ambient occlusion (GTAO, critical-path item 76). It attenuates
	// only the indirect radiance Forward+ writes to ForwardPlusTargets::Indirect.
	struct AmbientOcclusionSettings
	{
		bool Enabled = true;
		float Radius = 0.5f;			 // World-space search radius (> 0).
		float Falloff = 0.4f;			 // Fraction of Radius over which occluders fade out, (0, 1].
		float Power = 1.0f;				 // visibility^Power, (0, 8].
		float MaxRadiusPixels = 64.0f;	 // Screen-space cap of the search, [1, 256].
		std::uint32_t SliceCount = 2;	 // Directions per pixel, 1 .. MaxAoSlices.
		std::uint32_t StepCount = 4;	 // Samples per direction and side, 1 .. MaxAoSteps.
		float BlurDepthTolerance = 0.1f; // Relative depth difference at which the 5x5 blur stops mixing, (0, 1].
	};

	// Analytic exponential height fog with a sun in-scattering lobe (item 76).
	struct FogSettings
	{
		bool Enabled = false;
		float Density = 0.02f;							// Extinction per metre at BaseHeight (>= 0).
		float HeightFalloff = 0.1f;						// Density falls by e every 1 / HeightFalloff metres upward (>= 0; 0 = uniform).
		float BaseHeight = 0.0f;						// World Y of Density.
		std::array<float, 3> Color{ 0.5f, 0.6f, 0.7f }; // Ambient in-scattered radiance (linear, >= 0).
		std::array<float, 3> SunColor{ 0, 0, 0 };		// Directional in-scattered radiance at the phase peak (>= 0).
		std::array<float, 3> SunDirection{ 0, -1, 0 };	// Direction the sunlight travels (normalized when packed).
		float Anisotropy = 0.6f;						// Henyey-Greenstein g, [-0.95, 0.95].
		float StartDistance = 0.0f;						// Fog begins this far from the camera (>= 0).
		float MaxDistance = 1000.0f;					// Distances (and the sky) are capped here (> StartDistance).
	};

	struct ScreenSpaceSettings
	{
		AmbientOcclusionSettings AmbientOcclusion;
		FogSettings Fog;
	};

	// Throws std::invalid_argument for values outside the ranges above.
	void ValidateScreenSpaceSettings(const ScreenSpaceSettings& settings);
} // namespace Swim::Render
