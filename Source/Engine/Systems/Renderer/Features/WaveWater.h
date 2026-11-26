#pragma once

#include "Engine/Systems/Renderer/ScreenSpace/DistanceFog.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"
#include "Engine/Systems/Water/WaterWaves.h"

#include <array>
#include <functional>
#include <limits>
#include <memory>

namespace Engine
{

	// Water with Gerstner waves (WaterWaves.h), drawn in the transparent pass with the engine's
	// water shading (refraction, absorption, reflection, caustics, shore foam) on a camera-
	// centred grid whose spacing grows with distance (NearSpacing under the camera to Extent),
	// optionally limited to a rectangle. Level of detail is continuous (Lod): waves lower and the
	// short ones flatten with distance, ripples become roughness, reflection steps step down,
	// and a flat far-field ring carries the shading past the grid, so nothing pops or ends.
	// The waves are the same set physics floats bodies on (GetWaves / MakeFluidSurface): give
	// both the same clock (Clock). Program: WaveWater.
	class WaveWater final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			WaterWaveSettings Waves;
			std::array<float, 2> BoundsMin{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() }; // World xz.
			std::array<float, 2> BoundsMax{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
			float EdgeFade = 3.0f;				// m inside the bounds over which waves grow from flat (shores).
			float Extent = 600.0f;				// m from the camera to the grid's edge.
			float NearSpacing = 0.25f;			// m between vertices under the camera.
			std::uint32_t GridResolution = 192; // Quads per side (16..1024).
			WaterLodSettings Lod;
			std::array<float, 3> Absorption{ 0.45f, 0.06f, 0.03f }; // Per metre.
			std::array<float, 3> Scattering{ 0.004f, 0.012f, 0.012f };
			float Roughness = 0.04f;
			float Refraction = 1.0f;
			float Ripples = 1.0f;				 // Normal-only ripples under the shortest wave (0 = none).
			std::uint32_t ReflectionSteps = 16; // Near the camera; Lod steps them down.
			// Fog applied at the water's own distance, for scenes that fog the ground in their own
			// shaders: match their curve. With the screen-space scene fog on, leave it off.
			DistanceFogSettings Fog;
		};

		SettingsData Settings;

		// Seconds of wave time; empty: the renderer's clock. Physics: PhysicsWorld::GetSimulationTime.
		std::function<double()> Clock;

		std::string_view GetName() const override { return "Wave water"; }

		std::uint32_t GetGeometryPasses() const override;

		RenderGeometryResources PrepareGeometry(RenderFeatureContext& context) override;

		void DrawGeometry(RenderFeatureDrawContext& context) override;

		// The wave set of the current settings (rebuilt when they change).
		std::shared_ptr<const WaterWaveSet> GetWaves();

	  private:

		Swim::Render::GraphBuffer params{};
		std::uint32_t vertices = 0;
		std::shared_ptr<const WaterWaveSet> waves;
		WaterWaveSettings built{};
		bool hasBuilt = false;

	};

} // namespace Engine
