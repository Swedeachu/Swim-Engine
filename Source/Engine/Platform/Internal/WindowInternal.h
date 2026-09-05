#pragma once

#include "Engine/Platform/Window.h"
#include <SDL3/SDL_video.h>

namespace Swim::Platform
{

	struct Window::Impl
	{
		SDL_Window* Window = nullptr;
		bool ExternalWindow = false;
		bool ExternalParent = false;
		NativeWindowHandle Parent{};
		uint32_t ParentThreadId = 0;
		uint32_t ForegroundThreadId = 0;
		std::uintptr_t OriginalWindowProc = 0;
	};

	namespace Internal
	{
		struct WindowAccess
		{
			static SDL_Window* GetSdlWindow(Window& window)
			{
				return window.impl ? window.impl->Window : nullptr;
			}
		};
	}

}
