#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>

namespace Engine
{
	// Ray-marched volumetric clouds in a horizontal layer: procedural fBm density shaped by
	// Coverage and a cumulus height profile, lit by the sun (shadow march, Beer-Lambert,
	// powder, two-lobe phase) and the sky's ambient colors, marched at a reduced
	// resolution and composited over the sky before TAA (which resolves the dithering).
	// Wind scrolls the noise. Programs: VolumetricCloudsMarch, VolumetricCloudsComposite.
	class VolumetricClouds final : public RenderFeature
	{
	  public:
		struct SettingsData
		{
			float Coverage = 0.42f;		   // 0 (clear) .. 1 (overcast).
			float Density = 0.006f;		   // Extinction per meter inside a full-density cloud.
			float BottomAltitude = 450.0f; // World Y (meters) of the layer.
			float TopAltitude = 1300.0f;
			float ShapeScale = 1.0f / 900.0f;  // Noise frequency (1/m) of the cloud shapes.
			float DetailScale = 1.0f / 160.0f; // Noise frequency (1/m) of the eroding detail.
			float DetailErosion = 0.35f;
			std::array<float, 3> Wind{ 12.0f, 0.0f, 4.0f }; // Meters per second.
			float MaxDistance = 18000.0f;					// Farthest ray distance (m).
			float HorizonFade = 0.35f;						// Fading starts at this fraction of MaxDistance.
			float SunIntensity = 1.0f;						// Scales the sky's sun radiance.
			float AmbientStrength = 1.6f;					// Scales the sky colors used as ambient light.
			float Anisotropy = 0.55f;						// Forward-scattering g of the phase function.
			float SilverLining = 0.25f;						// Weight of the back-scattering lobe.
			float Powder = 0.5f;							// Dark-edge "powder" effect strength (0..1).
			std::uint32_t Steps = 48;						// Primary march steps.
			std::uint32_t ShadowSteps = 4;					// Sun shadow steps per sample.
			float ShadowStepLength = 70.0f;					// Meters.
			float ResolutionScale = 0.5f;					// March resolution relative to the viewport (0.25..1).
		};

		SettingsData Settings;

		std::string_view GetName() const override { return "Volumetric clouds"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforeTemporal; }

		void Record(RenderFeatureContext& context) override;

	  private:
		std::array<float, 3> windOffset{ 0, 0, 0 };
	};
} // namespace Engine
