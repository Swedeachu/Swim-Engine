#pragma once

#include <cstdint>

namespace Engine
{

	class OceanSimulation;
	class OceanSurface;
	class Underwater;

	enum class WaterQuality : std::uint8_t
	{
		Off,
		Low,
		Medium,
		High,
		Ultra,
	};

	// What each tier costs: simulation size, surface grid density and which underwater
	// passes run. Look parameters (colours, wind, intensities) are not part of a tier.
	struct WaterQualityPreset
	{
		bool Enabled = true;
		std::uint32_t OceanResolution = 256; // FFT texels per side.
		std::uint32_t OceanCascades = 3;
		std::uint32_t SurfaceGrid = 256; // OceanSurface quads per side.
		bool UnderwaterMedium = true;
		bool Caustics = true;
		bool ChromaticCaustics = false;
		std::uint32_t ShaftSteps = 12;
		bool UnderwaterLens = true;
		bool ChromaticAberration = true;
	};

	WaterQualityPreset GetWaterQualityPreset(WaterQuality quality);

	// Applies a tier's costs to whichever objects are given (null ones are skipped): the
	// simulation's resolution and cascade count, the surface grid and Enabled flag, and the
	// underwater passes and steps. Intensities stay the caller's: a tier whose Caustics or
	// ChromaticAberration is false expects the caller to drive that intensity to 0.
	void ApplyWaterQuality(WaterQuality quality, OceanSimulation* simulation, OceanSurface* surface, Underwater* underwater);

} // namespace Engine
