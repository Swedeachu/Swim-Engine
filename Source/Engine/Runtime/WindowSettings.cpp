#include "Engine/Runtime/WindowSettings.h"

#include <algorithm>
#include <charconv>
#include <iostream>

namespace Engine
{

	using Swim::Commands::VariableFlags;
	using Swim::Platform::WindowMode;

	namespace
	{

		constexpr const char* Category = "Video/Display";

	} // namespace

	WindowSettings::WindowSettings(Swim::Commands::CommandRegistry& commandsValue, Swim::Platform::WindowSystem& windowsValue,
		Swim::Platform::Window* windowValue, const Swim::Platform::WindowDesc& initial)
		: commands(commandsValue), windows(windowsValue), window(windowValue)
	{
		mode = &commands.AddEnum("window_mode", { "windowed", "borderless", "exclusive" }, static_cast<uint32_t>(initial.Mode),
			"Windowed, borderless fullscreen or exclusive fullscreen", Category, VariableFlags::Saved);
		display = &commands.AddInt("window_display", std::min<int64_t>(initial.DisplayIndex, 16), 0, 16, "Display to use: 0 = primary, n = the nth entry of display_list", Category,
			VariableFlags::Saved);
		std::string initialResolution;

		if (initial.FullscreenMode.Width && initial.FullscreenMode.Height)
		{
			initialResolution = std::to_string(initial.FullscreenMode.Width) + "x" + std::to_string(initial.FullscreenMode.Height);
		}

		resolution = &commands.AddString("window_resolution", initialResolution,
			"WxH[@Hz]: window size when windowed, video mode when exclusive (empty = desktop)", Category, VariableFlags::Saved);

		if (mode->GetEnum() != 0)
		{
			lastFullscreen = mode->GetEnum();
		}

		const auto apply = [this](const Swim::Commands::ConsoleVariable&)
		{
			Apply();
		};
		mode->Subscribe(apply);
		display->Subscribe(apply);
		resolution->Subscribe(apply);

		commands.Register({ "fullscreen_toggle", "Switches between windowed and the last fullscreen mode", Category, "" },
			[this](const std::vector<std::string>&)
			{
				ToggleFullscreen();
			});
		commands.Register({ "display_list", "Lists displays and their fullscreen modes", Category, "[modes]" },
			[this](const std::vector<std::string>& arguments)
			{
				const bool showModes = !arguments.empty();
				const auto displays = windows.GetDisplays();
				const uint32_t current = window ? window->GetDisplay() : 0;

				for (std::size_t i = 0; i < displays.size(); ++i)
				{
					const auto& info = displays[i];
					std::cout << (i + 1) << ": " << info.Name << " " << info.DesktopMode.Width << "x" << info.DesktopMode.Height << "@"
							  << info.DesktopMode.RefreshRate << (info.Primary ? " (primary)" : "") << (info.Id == current ? " (current)" : "")
							  << ", " << info.Modes.size() << " fullscreen modes\n";

					if (showModes)
					{
						for (const auto& m : info.Modes)
						{
							std::cout << "     " << m.Width << "x" << m.Height << "@" << m.RefreshRate << '\n';
						}
					}
				}
			});
	}

	WindowSettings::~WindowSettings()
	{
		commands.Unregister("fullscreen_toggle");
		commands.Unregister("display_list");
	}

	void WindowSettings::ToggleFullscreen()
	{
		if (mode->GetEnum() == 0)
		{
			mode->SetEnum(lastFullscreen);
		}
		else
		{
			lastFullscreen = mode->GetEnum();
			mode->SetEnum(0u);
		}
	}

	std::optional<Swim::Platform::DisplayModeDesc> WindowSettings::ParseResolution(std::string_view text)
	{
		const auto x = text.find_first_of("xX");

		if (x == std::string_view::npos)
		{
			return std::nullopt;
		}

		const auto at = text.find('@', x);
		const std::string_view widthText = text.substr(0, x);
		const std::string_view heightText = text.substr(x + 1, at == std::string_view::npos ? std::string_view::npos : at - x - 1);
		Swim::Platform::DisplayModeDesc result{};

		const auto parse = [](std::string_view part, auto& value)
		{
			const auto r = std::from_chars(part.data(), part.data() + part.size(), value);
			return !part.empty() && r.ec == std::errc() && r.ptr == part.data() + part.size();
		};

		if (!parse(widthText, result.Width) || !parse(heightText, result.Height) || result.Width < 16 || result.Height < 16)
		{
			return std::nullopt;
		}

		if (at != std::string_view::npos && !parse(text.substr(at + 1), result.RefreshRate))
		{
			return std::nullopt;
		}

		return result;
	}

	uint32_t WindowSettings::ResolveDisplay() const
	{
		const auto index = display->GetInt();

		if (index <= 0)
		{
			return 0;
		}

		const auto displays = windows.GetDisplays();
		return static_cast<std::size_t>(index) <= displays.size() ? displays[static_cast<std::size_t>(index) - 1].Id : 0;
	}

	void WindowSettings::Apply()
	{
		if (!window || applying)
		{
			return;
		}

		applying = true;
		const auto parsed = ParseResolution(resolution->GetString());

		if (!resolution->GetString().empty() && !parsed)
		{
			std::cerr << "[Window] window_resolution expects WxH or WxH@Hz, got '" << resolution->GetString() << "'\n";
		}

		const auto requested = static_cast<WindowMode>(mode->GetEnum());

		if (requested != WindowMode::Windowed)
		{
			lastFullscreen = mode->GetEnum();
		}

		window->MoveToDisplay(ResolveDisplay());

		if (requested == WindowMode::Windowed && parsed)
		{
			window->SetMode(WindowMode::Windowed);
			window->SetSize({ parsed->Width, parsed->Height });
		}
		else
		{
			window->SetMode(requested, parsed ? &*parsed : nullptr);
		}

		applying = false;
	}

} // namespace Engine
