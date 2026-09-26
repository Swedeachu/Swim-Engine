#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

namespace Engine
{
	// An analytic lens flare of the sun (glare, starburst, chromatic ghosts and a halo),
	// added to the HDR scene before exposure and bloom so it blooms with the frame. Its
	// visibility is measured from the frame (sky taps around the sun), so occluders and
	// clouds fade it without a query. Program: LensFlare.
	class LensFlare final : public RenderFeature
	{
	  public:
		struct SettingsData
		{
			float Intensity = 0.25f;	// Overall strength (relative to the sun's color).
			std::uint32_t Ghosts = 6;	// 0..8.
			float GhostSpacing = 0.22f; // Distance between ghosts along the flare axis.
			float GhostIntensity = 0.18f;
			float GlareSize = 0.02f; // Exponential falloff radius of the glare (screen height units).
			float Streaks = 6.0f;	 // Starburst streak pairs.
			float StarburstIntensity = 0.35f;
			float HaloRadius = 0.45f; // Screen height units from the center.
			float HaloIntensity = 0.12f;
			float ChromaticSpread = 0.06f; // Red/blue ghost size offset.
			float Threshold = 1.5f;		   // Sky luminance that counts as "sun visible".
			float OcclusionRadius = 0.02f; // Radius of the visibility taps (uv).
			float EdgeFade = 0.15f;		   // Fades as the sun leaves the screen by this much (uv).
		};

		SettingsData Settings;

		std::string_view GetName() const override { return "Lens flare"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforePostProcess; }

		int GetOrder() const override { return 20; }

		void Record(RenderFeatureContext& context) override;
	};
} // namespace Engine
