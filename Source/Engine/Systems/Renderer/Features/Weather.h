#pragma once

#include "Engine/Systems/Renderer/Features/Wind.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Engine
{

	enum class WeatherQuality : std::uint8_t
	{
		Low,
		Medium,
		High,
		Ultra,
	};

	// Rain and snow drawn on the GPU in the transparent pass, with no particle state: each
	// particle is a hash of a world-space column cell and its index, falling through a band
	// around the camera at a fixed rate and drifting with the wind, so the field is stable
	// in the world while it follows the camera. Drops are velocity-aligned streaks, flakes
	// soft discs that tumble; both are lit by the frame (sun, sky, local lights). Program:
	// Weather.
	//
	// SetOcclusion gives an optional height field (the top of whatever stops precipitation,
	// per cell): particles below it vanish and rain splashes on it. Advance(dt) integrates
	// surface wetness, which the game reads to darken and gloss its own materials.
	class Weather final : public RenderFeature
	{

	  public:
		struct SettingsData
		{
			float Intensity = 0.0f; // 0..1: share of the particle budget that falls.
			float Snow = 0.0f;		// 0 rain .. 1 snow; in between, a mix.
			std::uint32_t MaxParticles = 16384;
			float Radius = 20.0f; // Metres around the camera.
			float Height = 24.0f; // Metres of the band the particles fall through.
			float RainSpeed = 9.0f;
			float SnowSpeed = 1.1f;
			float DropLength = 0.55f;
			float DropWidth = 0.01f;
			float FlakeSize = 0.035f;
			float Opacity = 0.5f;	 // Of a drop at full intensity; flakes are denser.
			float Brightness = 1.0f; // Scales the lit colour.
			std::array<float, 3> Tint{ 1.0f, 1.0f, 1.0f };
			float WindInfluence = 1.0f; // 0: straight down; 1: carried by the wind.
			bool Splashes = true;		// Needs an occlusion height field.
			std::uint32_t MaxSplashes = 1024;
			float SplashRadius = 10.0f;
			float SplashSize = 0.12f;
			float WetnessRise = 0.05f; // Per second toward the rain intensity.
			float WetnessDry = 0.01f;  // Per second back to dry.
		};

		SettingsData Settings;

		// The wind that carries the particles (share it with everything else the wind moves).
		WindSettings Wind;

		std::string_view GetName() const override { return "Weather"; }

		std::uint32_t GetGeometryPasses() const override { return RenderGeometryPassBit(RenderGeometryPass::Transparent); }

		RenderGeometryResources PrepareGeometry(RenderFeatureContext& context) override;

		void DrawGeometry(RenderFeatureDrawContext& context) override;

		// Particle budgets and splashes for a quality level.
		void ApplyQuality(WeatherQuality quality);

		// A size x size field of heights (metres, row-major in z then x) whose cell (0, 0)
		// starts at (originX, originZ). Heights below every particle (for example -1e9) mean
		// open sky. An empty span removes the field.
		void SetOcclusion(float originX, float originZ, float cellSize, std::uint32_t size, std::span<const float> heights);

		// Integrates wetness over dt seconds of game time; call every frame even while the
		// feature is disabled, so surfaces keep drying.
		void Advance(float dt);

		// 0 dry .. 1 soaked.
		float GetWetness() const { return wetness; }

		// 0..1: how much the precipitation should thicken fog (snow more than rain).
		float GetFogDensity() const;

	  private:
		std::vector<float> occlusion{ -1.0e9f };
		std::array<float, 4> occlusionArea{}; // Origin x, origin z, cell size, size (0: none).
		Swim::Render::GraphBuffer occlusionBuffer{};
		std::uint32_t particles = 0;
		std::uint32_t columns = 0;
		std::uint32_t perColumn = 0;
		std::uint32_t splashes = 0;
		float wetness = 0.0f;
	};

} // namespace Engine
