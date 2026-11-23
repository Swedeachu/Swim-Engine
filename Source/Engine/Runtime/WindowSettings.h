#pragma once

#include "Engine/Commands/CommandRegistry.h"
#include "Engine/Platform/WindowSystem.h"

#include <optional>

namespace Engine
{

	// Window mode, display and resolution as saved console variables, applied to the engine
	// window when they change:
	//   window_mode windowed|borderless|exclusive
	//   window_display <n>         0 = primary, n = the nth entry of display_list
	//   window_resolution WxH[@Hz] window size when windowed, video mode when exclusive
	//                              (empty = the desktop mode / the current size)
	//   fullscreen_toggle          windowed <-> the last fullscreen mode (borderless first)
	//   display_list               displays and their exclusive modes
	// Without a window (headless) the variables exist and are saved but apply nothing.
	class WindowSettings
	{

	  public:

		WindowSettings(Swim::Commands::CommandRegistry& commands, Swim::Platform::WindowSystem& windows, Swim::Platform::Window* window,
			const Swim::Platform::WindowDesc& initial);

		~WindowSettings();

		WindowSettings(const WindowSettings&) = delete;

		WindowSettings& operator=(const WindowSettings&) = delete;

		void ToggleFullscreen();

		// "1920x1080", "1920x1080@144" -> mode; empty or malformed -> nullopt.
		static std::optional<Swim::Platform::DisplayModeDesc> ParseResolution(std::string_view text);

	  private:

		void Apply();

		uint32_t ResolveDisplay() const;

		Swim::Commands::CommandRegistry& commands;
		Swim::Platform::WindowSystem& windows;
		Swim::Platform::Window* window;
		Swim::Commands::ConsoleVariable* mode = nullptr;
		Swim::Commands::ConsoleVariable* display = nullptr;
		Swim::Commands::ConsoleVariable* resolution = nullptr;
		uint32_t lastFullscreen = 1; // Borderless.
		bool applying = false;

	};

} // namespace Engine
