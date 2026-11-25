#pragma once

#include "Engine/Systems/Physics/PhysicsBuoyancy.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>

namespace Engine
{

	// A wind sea as a few travelling Gerstner waves, evaluated the same way on the CPU (here:
	// physics, gameplay) and the GPU (Water/WaterWaves.slang: WaveWater), so floating bodies
	// ride the waves that are drawn. Wave i is shorter and lower than wave i - 1 and its
	// direction spreads around the wind.
	struct WaterWaveSettings
	{
		float Height = 0.0f;				// Mean water level (world y).
		float WindDirectionDegrees = 30.0f; // From +X toward +Z.
		float Wavelength = 14.0f;			// m, the longest wave.
		float Amplitude = 0.3f;				// m, the longest wave's height above the mean.
		float Steepness = 0.55f;			// 0 = rounded swells, 1 = sharp crests (no loops).
		float Spread = 0.7f;				// Radians: how far the directions scatter around the wind.
		float Shortening = 0.62f;			// Each next wave's length (and height) relative to the previous one.
		std::uint32_t Count = 6;			// 1..MaxWaves.
		float TimeScale = 1.0f;
		std::uint32_t Seed = 1;
	};

	struct WaterWave
	{
		glm::vec2 Direction{ 1.0f, 0.0f };
		float Number = 1.0f;		 // 2 pi / wavelength.
		float Frequency = 1.0f;		 // Angular (rad/s): sqrt(g k), times TimeScale.
		float Amplitude = 0.0f;		 // m, vertical.
		float Horizontal = 0.0f;	 // m, horizontal (Gerstner steepness).
		float Phase = 0.0f;
		float Detail = 0.0f; // 0 for the longest wave .. 1 for the shortest (fades first with distance).
	};

	struct WaterWaveSet
	{
		static constexpr std::uint32_t MaxWaves = 8;

		std::array<WaterWave, MaxWaves> Waves{};
		std::uint32_t Count = 0;
		float Height = 0.0f;
	};

	WaterWaveSet BuildWaterWaves(const WaterWaveSettings& settings);

	// Where the surface point that rests at world xz moves to (world position) at a time;
	// weights scale each wave (LOD; null: all 1).
	glm::vec3 EvaluateWaterWaves(const WaterWaveSet& set, glm::vec2 rest, double time, const float* weights = nullptr);

	struct WaterSurfaceSample
	{
		float Height = 0.0f;
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Velocity{ 0.0f }; // Of the water at the surface (orbital motion).
	};

	// The surface above world xz (the rest point whose displaced position lands there is
	// found by fixed-point iteration), at full detail.
	WaterSurfaceSample SampleWaterSurface(const WaterWaveSet& set, float x, float z, double time);

	// Fluid surface for PhysicsWorld buoyancy that follows the waves (time: the physics
	// world's simulated seconds, so draw the water with the same clock).
	FluidSurfaceFunction MakeFluidSurface(std::shared_ptr<const WaterWaveSet> set);

	// Continuous level of detail for water surfaces: everything fades with the distance from
	// the camera instead of switching, so the water never pops or ends. Distances are metres
	// from the camera; each fade runs smoothly from its Start (full) to its End (faded).
	struct WaterLodSettings
	{
		// The defaults thin detail gradually across the first ~200 m (smoothstep fades, so the
		// near field is barely touched and there is no step anywhere), then lower the waves.
		bool Enabled = true;
		float DetailStart = 0.0f; // The shorter waves flatten out, the shortest first.
		float DetailEnd = 200.0f;
		float AmplitudeStart = 200.0f; // All waves lower toward FarAmplitude.
		float AmplitudeEnd = 1000.0f;
		float FarAmplitude = 0.15f;	  // Fraction of the wave height kept in the far field.
		float NormalStart = 0.0f;	  // Fine normal detail (ripples) fades; roughness widens to keep the shimmer.
		float NormalEnd = 250.0f;
		float FarRoughness = 0.16f;	  // Added to the roughness where normal detail is gone.
		float ReflectionStart = 0.0f; // Screen-space reflection steps step down to 0 (environment only).
		float ReflectionEnd = 300.0f;
		bool FarField = true;		   // A flat, still shaded ring from the grid's edge to FarFieldExtent.
		float FarFieldExtent = 8000.0f;
		bool Debug = false; // Tints the surface: red = full detail, green = detail faded, blue = far field.
	};

	struct WaterLodFactors
	{
		float Detail = 1.0f;	 // Weight of the shortest wave (longer waves interpolate toward 1).
		float Amplitude = 1.0f;	 // Multiplies every wave.
		float Normal = 1.0f;	 // Fine normal detail.
		float Reflection = 1.0f; // Fraction of the reflection steps.
	};

	// Throws std::invalid_argument for negative or non-finite distances, an End before its
	// Start, or FarAmplitude / FarRoughness outside [0, 1].
	void ValidateWaterLodSettings(const WaterLodSettings& settings);

	WaterLodFactors EvaluateWaterLod(const WaterLodSettings& settings, float distance);

	// Per-wave weights at a distance: Amplitude x lerp(1, Detail, wave.Detail).
	void WaterWaveLodWeights(const WaterWaveSet& set, const WaterLodFactors& lod, float* weights);

} // namespace Engine
