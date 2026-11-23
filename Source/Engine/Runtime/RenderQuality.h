#pragma once

#include "Engine/Commands/CommandRegistry.h"

namespace Engine
{

	struct RenderSettings;

	// Anti-aliasing, sharpening and the render presets as saved console variables
	// (category "Video/Rendering"):
	//   render_aa off|fxaa|smaa|taa  RenderSettings::AntiAliasing (smaa: morphological 1x)
	//   render_sharpen 0..1          contrast-adaptive sharpening after anti-aliasing
	//   render_bloom 0|1             PostProcessSettings::Bloom.Enabled
	//   render_cinematic 0|1         render features with IsCinematic() (depth of field, lens,
	//                                film effects) run; off skips them
	//   render_preset quality|balanced|competitive|custom
	//                                sets the variables above and video_low_latency; changing
	//                                any of them by hand switches the preset to custom
	class RenderQuality
	{

	  public:

		explicit RenderQuality(Swim::Commands::CommandRegistry& commands);

		RenderQuality(const RenderQuality&) = delete;

		RenderQuality& operator=(const RenderQuality&) = delete;

		// The settings the variables write (null: none); the current values apply at once.
		void SetSettings(RenderSettings* settings);

	  private:

		void ApplyPreset();

		void Apply();

		void Changed();

		Swim::Commands::CommandRegistry& commands;
		Swim::Commands::ConsoleVariable* preset = nullptr;
		Swim::Commands::ConsoleVariable* antiAliasing = nullptr;
		Swim::Commands::ConsoleVariable* sharpen = nullptr;
		Swim::Commands::ConsoleVariable* bloom = nullptr;
		Swim::Commands::ConsoleVariable* cinematic = nullptr;
		RenderSettings* settings = nullptr;
		bool applyingPreset = false;

	};

} // namespace Engine
