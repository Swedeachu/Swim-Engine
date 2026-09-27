#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{
	// Black holes as a render feature: per pixel, the light ray is traced backward through
	// each hole's region as a Schwarzschild null geodesic (a = -1.5 R_s h^2 x / r^5), so the
	// shadow, the lensed sky and scene, Einstein rings and the photon ring come out of one
	// integration, and it crosses a volumetric, rainbow-emitting gas (a differentially
	// rotating accretion torus and inclined electron-shell rings with clumps racing around
	// them). Linear HDR before depth of field, the camera lens, exposure and bloom, so the
	// gas blooms. Gameplay sets Lenses each frame (a behaviour on the hole's entity does).
	// Program: GravitationalLensing.
	class GravitationalLensing final : public RenderFeature
	{
	  public:
		static constexpr std::uint32_t MaxLenses = 4;	// GravitationalLensing.slang MaxLenses.
		static constexpr std::uint32_t MaxSteps = 240;	// Geodesic steps per ray.
		static constexpr float GasStep = 0.4f;			// Largest step inside the gas (R_s).

		using Float3 = std::array<float, 3>;

		struct Lens
		{
			Float3 Position{ 0.0f, 0.0f, 0.0f };
			float SchwarzschildRadius = 1.0f; // R_s, metres.
			float Strength = 1.0f;			  // Scales the bending (1: general relativity's).
			float Reach = 12.0f;			  // Radius of the traced region (R_s): the bending builds up inside it; rays outside cost nothing.
			float GasRadius = 9.0f;			  // Outer radius of the gas (R_s); the torus starts near 3 R_s.
			float GasDensity = 1.0f;		  // 0: no gas.
			float GasBrightness = 3.0f;		  // Emission scale (HDR).
			float GasSpeed = 2.0f;			  // Orbital angular speed at 3 R_s (rad/s); Kepler falloff outward.
			std::uint32_t Orbits = 3;		  // Inclined electron-shell rings (0..3).
			Float3 DiskNormal{ 0.0f, 1.0f, 0.0f };
			std::uint64_t Owner = 0; // Who set it (Upsert/Remove), 0: unowned.
		};

		std::vector<Lens> Lenses; // The first MaxLenses are used.

		// The lens `owner` keeps (created on first use), and its removal.
		Lens& Upsert(std::uint64_t owner);
		void Remove(std::uint64_t owner);

		std::string_view GetName() const override { return "Gravitational lensing"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforePostProcess; }

		int GetOrder() const override { return 5; } // Before the sun shafts, depth of field and the camera lens.

		void Record(RenderFeatureContext& context) override;

		// The photon-capture impact parameter (3 sqrt 3 / 2) R_s: the radius of the shadow.
		static float ShadowRadius(float schwarzschildRadius);
		// The weak-field deflection angle (radians) at impact parameter b: 2 R_s / b.
		static float Deflection(float schwarzschildRadius, float impact);
		// The Einstein ring's angular radius for a source at infinity: sqrt(2 R_s / D).
		static float EinsteinAngle(float schwarzschildRadius, float lensDistance);

		// The shader's geodesic (CPU definition): a ray from `origin` (relative to the hole)
		// along `direction`, traced until it falls in, leaves the sphere of `region` or runs
		// out of steps (an orbit near the photon sphere).
		struct Trace
		{
			bool Captured = false;
			bool Escaped = false;
			Float3 Direction{};	 // Final direction (unit).
			std::uint32_t Steps = 0;
		};

		static Trace TraceRay(const Float3& origin, const Float3& direction, float schwarzschildRadius, float region);
		// The gas density at `position` (relative to the hole, in R_s) at `time` (the shader's
		// Gas(...).Density).
		static float GasDensityAt(const Float3& position, float time, const Lens& lens);
	};
} // namespace Engine
