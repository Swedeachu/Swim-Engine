#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace Engine
{

	// A physically inspired sun, moon and night sky drawn over the sky pixels of the scene
	// (before the volumetric clouds, which then cover it): a sun disc with limb darkening, a
	// moon lit by the sun at any phase with faint earthshine, thousands of procedural stars
	// with magnitude and colour-temperature variation, twinkle and horizon scintillation, a
	// galaxy band with dust lanes, a few distant galaxies and nebulae, and meteors. The stars
	// and galaxy live on a celestial sphere that turns with SiderealAngle about a pole tilted
	// by Latitude, so they wheel through the night. Everything fades with the atmosphere's
	// airmass near the horizon and with daylight (stars come out as the sun sinks below the
	// horizon), and Visibility (0 under an overcast sky) hides the night sky entirely.
	// The galaxy, nebulae and distant galaxies are baked once into a small sky texture; the
	// per-pixel cost is one texture lookup, a 3x3 star-cell search and the discs.
	// Programs: CelestialSkyBake, CelestialSky.
	class CelestialSky final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			// Sun: drawn when the view's sun is above the horizon.
			bool Sun = true;
			float SunSize = 0.27f;		  // Angular radius (degrees); the real sun is 0.27.
			float SunIntensity = 1.0f;	  // Disc radiance relative to the view's sun colour.
			float LimbDarkening = 0.6f;	  // 0: flat disc .. 1: strongly darker toward its edge.
			// Moon.
			bool Moon = true;
			std::array<float, 3> MoonDirection{ 0.0f, 0.0f, 0.0f }; // Toward the moon; zero: opposite the sun.
			float MoonPhase = 0.5f;		  // 0: new .. 0.5: full .. 1: new again (waxing, then waning).
			float MoonSize = 0.26f;		  // Angular radius (degrees); the real moon is 0.26.
			float MoonBrightness = 1.0f;  // Radiance of the full moon (scene units).
			float Earthshine = 0.01f;	  // Light on the moon's dark side, relative to its lit side.
			// Stars.
			float StarDensity = 1.0f;	  // 0..3: about 9000 stars at 1.
			float StarBrightness = 1.0f;  // Scene radiance of the brightest stars, scaled.
			float Twinkle = 0.5f;		  // 0..1: scintillation (much stronger toward the horizon).
			// Galaxy band and deep sky.
			float GalaxyIntensity = 1.0f; // 0 hides the band.
			float GalaxyTilt = 60.0f;	  // Degrees between the band's plane and the celestial equator.
			float GalaxyRotation = 0.0f;  // Degrees: where along the sky the band's bright core sits.
			float NebulaIntensity = 1.0f; // Nebulae and distant galaxies.
			// The celestial sphere: the stars turn about a pole Latitude degrees above the north
			// horizon (-Z), by SiderealAngle radians (advance it with the time of day).
			float Latitude = 45.0f;
			float SiderealAngle = 0.0f;
			// Meteors.
			float MeteorRate = 2.0f;	  // Per minute while the stars are out (0 = none).
			float MeteorBrightness = 1.0f;
			// Overall.
			float Visibility = 1.0f;	  // 1: clear .. 0: overcast (stars, galaxy and moon hidden).
			float NightIntensity = 1.0f;  // Scales stars, galaxy, nebulae and meteors together.
		};

		SettingsData Settings;

		std::string_view GetName() const override { return "Celestial sky"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforeTemporal; }

		// Before the clouds and everything else drawn over the sky.
		int GetOrder() const override { return -100; }

		void Record(RenderFeatureContext& context) override;

	  private:

		struct Meteor
		{
			std::array<float, 3> Start;
			std::array<float, 3> End;
			float Age = 0.0f;
			float Duration = 0.0f;
			float Brightness = 0.0f;
		};

		static constexpr std::uint32_t MaxMeteors = 4;
		static constexpr std::uint32_t BakeWidth = 2048;
		static constexpr std::uint32_t BakeHeight = 1024;

		std::unique_ptr<Swim::Rhi::Texture> deepSky;
		bool baked = false;
		const void* bakedWith = nullptr;
		std::vector<Meteor> meteors;
		float meteorTimer = 0.0f;
		std::mt19937 random{ 1234u };

	};

} // namespace Engine
