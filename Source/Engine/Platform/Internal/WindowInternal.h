#pragma once

#include "Engine/Platform/Window.h"
#include <SDL3/SDL_video.h>

namespace Swim::Platform
{

	struct Window::Impl
	{
		SDL_Window* Window = nullptr;
		bool ExternalWindow = false;
		bool CaptureMouse = true;
		bool ExternalParent = false;
		NativeWindowHandle Parent{};
		uint32_t ParentThreadId = 0;
		uint32_t ForegroundThreadId = 0;
		std::uintptr_t OriginalWindowProc = 0;
	};

	namespace Internal
	{

		// SDL window property: relative mouse mode requested by the application (kept
		// across focus changes, which release the mode itself).
		inline constexpr const char* RelativeMouseRequestedProperty = "Swim.RelativeMouseRequested";

		struct WindowAccess
		{
			static SDL_Window* GetSdlWindow(Window& window)
			{
				return window.impl ? window.impl->Window : nullptr;
			}
		};

	}

}
