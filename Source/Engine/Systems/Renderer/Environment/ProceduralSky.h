#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentMath.h"

#include <cstdint>

namespace Swim::Render::Environment
{
	// An analytic HDR sky (item 61's built-in environment source): a zenith-horizon
	// gradient above the horizon, a horizon-ground gradient below it and a smooth
	// sun lobe, SunColor * max(dot(d, sun), 0)^SunSharpness. Everything is linear
	// radiance. EnvironmentSky.slang evaluates the same function.
	struct ProceduralSky
	{
		Float3 ZenithColor{ 0.18f, 0.36f, 0.85f };
		Float3 HorizonColor{ 0.75f, 0.82f, 0.95f };
		Float3 GroundColor{ 0.22f, 0.2f, 0.18f };
		Float3 SunDirection{ 0.15f, 0.3f, 1.0f }; // Toward the sun; normalized on use.
		Float3 SunColor{ 12.0f, 11.0f, 9.5f };
		float SunSharpness = 48.0f;
		float Intensity = 1.0f; // Scales everything.

		Float3 Evaluate(const Float3& direction) const;

		// A uniform environment of the given radiance (the white furnace).
		static ProceduralSky Uniform(float radiance);
	};

	// EnvironmentSky.slang's push constants (std430, 96 bytes).
	struct ProceduralSkyConstants
	{
		float Zenith[4];		// w: intensity.
		float Horizon[4];		// w: unused.
		float Ground[4];		// w: unused.
		float SunDirection[4];	// Normalized; w: sharpness.
		float SunColor[4];		// w: unused.
		std::uint32_t Face = 0; // Layer being written.
		std::uint32_t Size = 0; // Face size in texels.
		std::uint32_t Reserved[2] = {};
	};

	static_assert(sizeof(ProceduralSkyConstants) == 96);

	ProceduralSkyConstants MakeProceduralSkyConstants(const ProceduralSky& sky, std::uint32_t face, std::uint32_t size);
} // namespace Swim::Render::Environment
