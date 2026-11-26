#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{

	// Volumetric fire, fireballs, smoke, heat haze and shockwave distortion as one render
	// feature: per pixel, the analytic volumes the view ray crosses in front of the scene are
	// raymarched front to back (procedural noise density, blackbody emission, sun-lit smoke),
	// flames throw analytic embers, and haze/shockwaves bend the background. Linear HDR before
	// temporal anti-aliasing, so bloom and TAA treat it like the scene. Nothing is recorded
	// while Volumes is empty. Usually driven by Engine::VfxDirector, which rebuilds Volumes
	// every frame. Program: PyroVolumes.
	class PyroVolumes final : public RenderFeature
	{

	  public:
		static constexpr std::uint32_t MaxVolumes = 64; // PyroVolumes.slang MaxVolumes; the rest are dropped.

		using Float3 = std::array<float, 3>;

		enum class Type : std::uint32_t
		{
			Flame = 0,	   // A column from Position (its base) up Height, Radius wide at the base.
			Fireball = 1,  // A sphere of Radius around Position; Age 0..1 burns it out into smoke.
			Smoke = 2,	   // A rising column from Position up Height, widening to Radius.
			Haze = 3,	   // Heat shimmer only: an ellipsoid of Radius (horizontal) and Height (vertical half extent).
			Shockwave = 4, // Distortion only: a shell of Radius around Position; Age 0..1 fades it.
		};

		struct Volume
		{
			Type Kind = Type::Flame;
			Float3 Position{ 0.0f, 0.0f, 0.0f };
			float Height = 1.0f;
			float Radius = 0.5f;
			float Age = 0.0f;				 // 0..1 (fireball, smoke, shockwave).
			Float3 Tint{ 1.0f, 1.0f, 1.0f }; // Fire colour when TintMix > 0 (e.g. a blue flame).
			float TintMix = 0.0f;			 // 0: blackbody colours; 1: Tint at the blackbody's brightness.
			float Intensity = 1.0f;			 // Emission scale.
			float Density = 1.0f;
			float RiseSpeed = 1.5f;					// Noise advection upward (1/s).
			float Seed = 0.0f;						// Decorrelates neighbouring volumes.
			float Distortion = 0.0f;				// Heat haze / shockwave strength; flames shimmer above themselves.
			std::uint32_t Embers = 0;				// Flame: rising embers (0..32).
			float SmokeAlbedo = 0.35f;				// Smoke and burnt-out fireball brightness.
			Float3 SmokeGlow{ 1.0f, 0.45f, 0.12f }; // Smoke: fire light from below...
			float SmokeGlowStrength = 0.0f;			// ...and its strength.
		};

		struct QualitySettings
		{
			std::uint32_t Steps = 20;	// March steps per volume.
			std::uint32_t Octaves = 3;	// Noise octaves (1..4).
			float MaxDistance = 160.0f; // Volumes farther than this are skipped.
		};

		std::vector<Volume> Volumes;
		QualitySettings Quality;

		std::string_view GetName() const override { return "Pyro volumes"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforeTemporal; }

		int GetOrder() const override { return 20; } // After the sky and the clouds.

		void Record(RenderFeatureContext& context) override;
	};

} // namespace Engine
