#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

namespace Engine
{
	// Screen-space sun shafts ("god rays"): bright sky around the sun is blurred radially
	// toward the sun's screen position and added to the HDR scene before exposure and
	// bloom. Geometry and clouds in front of the sun block the shafts because the mask only
	// keeps sky pixels brighter than Threshold. Programs: SunShaftsMask, SunShaftsComposite.
	class SunShafts final : public RenderFeature
	{
	  public:
		struct SettingsData
		{
			float Intensity = 0.025f;	// Scales the sun's radiance.
			float Density = 0.9f;		// Blur length as a fraction of the pixel-to-sun distance.
			float Decay = 0.965f;		// Per-sample falloff along the blur.
			float Weight = 0.025f;		// Per-sample contribution.
			std::uint32_t Samples = 64; // Blur samples (1..128).
			float ConeDegrees = 14.0f;	// Radius of the sky region around the sun that casts shafts.
			float Threshold = 1.5f;		// Scene luminance a mask pixel must exceed (sky near the sun is brighter).
			float MaskScale = 0.5f;		// Mask resolution relative to the viewport (0.25..1).
			float EdgeFade = 0.25f;		// The effect fades out as the sun leaves the screen by this much (uv).
		};

		SettingsData Settings;

		std::string_view GetName() const override { return "Sun shafts"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforePostProcess; }

		int GetOrder() const override { return 10; }

		void Record(RenderFeatureContext& context) override;
	};
} // namespace Engine
