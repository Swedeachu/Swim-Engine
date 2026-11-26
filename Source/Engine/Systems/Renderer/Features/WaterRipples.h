#pragma once

#include <array>
#include <cstdint>

namespace Engine
{

	// One expanding ring on a water surface, as the water shader reads it
	// (WaterRippleRecord in Water/WaterRipples.slang, std430, 32 bytes). A wave packet
	// travelling outward from Centre: height = Amplitude * envelope * cos(2 pi phase /
	// Wavelength), where phase is the distance behind the front at Speed * Age and the
	// envelope is a Gaussian of Width metres, damped with age and spread. Amplitude already
	// includes the distance fade (LOD), so the shader just sums the records.
	struct GpuWaterRipple
	{
		std::array<float, 4> Centre{}; // xyz: impact point on the surface; w: age (s).
		std::array<float, 4> Wave{};   // x: amplitude (m); y: speed (m/s); z: wavelength (m); w: width (m).
	};

	static_assert(sizeof(GpuWaterRipple) == 32);

	// How much splash and ripple work each impact gets.
	enum class WaterSplashQuality : std::uint8_t
	{
		Low,	// One ring, a small droplet burst.
		Medium, // Two rings, droplets and foam.
		High,	// Three rings, crown sheet, droplets, mist and foam.
	};

	struct WaterSplashSettings
	{
		WaterSplashQuality Quality = WaterSplashQuality::High;
		std::uint32_t MaxRipples = 64; // Rings sent to the shader (nearest first); keep <= the shader's buffer.
		float FadeStart = 24.0f;	   // Splashes start thinning out past this distance (m)...
		float FadeEnd = 64.0f;		   // ...and are skipped entirely past this.
		float RippleScale = 1.0f;	   // Ring amplitude multiplier.
		float SplashScale = 1.0f;	   // Particle count and size multiplier.
	};

} // namespace Engine
