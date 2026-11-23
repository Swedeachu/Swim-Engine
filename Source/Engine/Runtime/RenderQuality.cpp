#include "Engine/Runtime/RenderQuality.h"

#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"

#include <algorithm>
#include <cmath>

namespace Engine
{

	using Swim::Commands::VariableFlags;

	namespace
	{

		constexpr const char* Category = "Video/Rendering";

		struct Preset
		{
			AntiAliasingMode AntiAliasing;
			float Sharpen;
			bool Bloom;
			bool Cinematic;
			bool LowLatency;
		};

		// quality, balanced, competitive (custom keeps whatever is set).
		constexpr Preset Presets[] = {
			{ AntiAliasingMode::Taa, 0.0f, true, true, false },
			{ AntiAliasingMode::Taa, 0.25f, true, false, false },
			{ AntiAliasingMode::Smaa1x, 0.4f, false, false, true },
		};
		constexpr std::uint32_t Custom = 3;

	} // namespace

	RenderQuality::RenderQuality(Swim::Commands::CommandRegistry& commandsValue) : commands(commandsValue)
	{
		antiAliasing = &commands.AddEnum("render_aa", { "off", "fxaa", "smaa", "taa" }, 3,
			"Anti-aliasing: fxaa and smaa (morphological) work on the finished frame without history; taa resolves jittered frames",
			Category, VariableFlags::Saved);
		sharpen = &commands.AddFloat("render_sharpen", 0.0, 0.0, 1.0, "Contrast-adaptive sharpening after anti-aliasing (0 = off)", Category,
			VariableFlags::Saved);
		bloom = &commands.AddBool("render_bloom", true, "Bloom", Category, VariableFlags::Saved);
		cinematic = &commands.AddBool("render_cinematic", true, "Cinematic render features (depth of field, lens and film effects)", Category,
			VariableFlags::Saved);
		preset = &commands.AddEnum("render_preset", { "quality", "balanced", "competitive", "custom" }, 0,
			"Sets render_aa, render_sharpen, render_bloom, render_cinematic and video_low_latency; competitive favours clarity and latency",
			Category, VariableFlags::Saved);

		for (auto* variable : { antiAliasing, sharpen, bloom, cinematic })
		{
			variable->Subscribe(
				[this](const Swim::Commands::ConsoleVariable&)
				{
					Changed();
				});
		}

		preset->Subscribe(
			[this](const Swim::Commands::ConsoleVariable&)
			{
				ApplyPreset();
			});
	}

	void RenderQuality::SetSettings(RenderSettings* settingsValue)
	{
		settings = settingsValue;
		Apply();
	}

	void RenderQuality::ApplyPreset()
	{
		const auto index = preset->GetEnum();

		if (index >= Custom)
		{
			return;
		}

		const auto& values = Presets[index];
		applyingPreset = true;
		antiAliasing->SetEnum(static_cast<std::uint32_t>(values.AntiAliasing));
		sharpen->SetFloat(values.Sharpen);
		bloom->SetBool(values.Bloom);
		cinematic->SetBool(values.Cinematic);

		if (auto* lowLatency = commands.FindVariable("video_low_latency"))
		{
			lowLatency->SetBool(values.LowLatency);
		}

		applyingPreset = false;
		Apply();
	}

	void RenderQuality::Changed()
	{
		if (!applyingPreset)
		{
			const auto index = preset->GetEnum();

			if (index < Custom)
			{
				const auto& values = Presets[index];
				const bool matches = antiAliasing->GetEnum() == static_cast<std::uint32_t>(values.AntiAliasing) &&
					std::abs(sharpen->GetFloat() - values.Sharpen) < 1.0e-4f && bloom->GetBool() == values.Bloom &&
					cinematic->GetBool() == values.Cinematic;

				if (!matches)
				{
					preset->SetEnum(Custom);
				}
			}
		}

		Apply();
	}

	void RenderQuality::Apply()
	{
		if (!settings)
		{
			return;
		}

		settings->AntiAliasing = static_cast<AntiAliasingMode>(std::min<std::uint32_t>(antiAliasing->GetEnum(), 3));
		settings->Sharpening = sharpen->GetFloat();
		settings->Post.Bloom.Enabled = bloom->GetBool();
		settings->CinematicEffects = cinematic->GetBool();
	}

} // namespace Engine
