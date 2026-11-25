#include "Window.h"
#include "Internal/WindowInternal.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
	#include "Internal/WindowsApi.h"
#endif

namespace Swim::Platform
{


	namespace
	{

		NativeWindowHandle QueryNativeHandle(SDL_Window* window)
		{
			NativeWindowHandle handle{};

			if (!window)
			{
				return handle;
			}

			const SDL_PropertiesID properties = SDL_GetWindowProperties(window);

		#if defined(_WIN32)
			handle.Type = NativeWindowType::Win32;
			handle.Window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
		#elif defined(__APPLE__)
			handle.Type = NativeWindowType::Cocoa;
			handle.Window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
		#elif defined(__linux__)
			void* waylandSurface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);

			if (waylandSurface)
			{
				handle.Type = NativeWindowType::Wayland;
				handle.Window = waylandSurface;
				handle.Display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
			}
			else
			{
				handle.Type = NativeWindowType::X11;
				handle.Window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
					SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)
				));
				handle.Display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
			}

		#endif

			if (!handle.Window)
			{
				handle = {};
			}

			return handle;
		}


	}

	Window::Window(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
	Window::Window(Window&&) noexcept = default;
	Window& Window::operator=(Window&&) noexcept = default;

	Window::~Window()
	{
		if (!impl)
		{
			return;
		}

	#if defined(_WIN32)

		if (impl->ExternalParent)
		{
			const NativeWindowHandle native = QueryNativeHandle(impl->Window);
			HWND hwnd = static_cast<HWND>(native.Window);

			if (hwnd && impl->OriginalWindowProc != 0)
			{
				RemovePropW(hwnd, L"SwimEngine.EmbeddedOriginalWindowProc");
				SetWindowLongPtrW(hwnd, GWLP_WNDPROC, static_cast<LONG_PTR>(impl->OriginalWindowProc));
			}

			const DWORD currentThreadId = GetCurrentThreadId();

			if (impl->ForegroundThreadId != 0)
			{
				AttachThreadInput(currentThreadId, impl->ForegroundThreadId, FALSE);
			}

			if (impl->ParentThreadId != 0)
			{
				AttachThreadInput(currentThreadId, impl->ParentThreadId, FALSE);
			}
		}

	#endif

		if (impl->Window)
		{
			SDL_DestroyWindow(impl->Window);
			impl->Window = nullptr;
		}
	}

	WindowId Window::GetId() const
	{
		return impl && impl->Window ? static_cast<WindowId>(SDL_GetWindowID(impl->Window)) : 0;
	}

	Extent2D Window::GetLogicalSize() const
	{
		Extent2D size{};

		if (!impl || !impl->Window)
		{
			return size;
		}

		int width = 0;
		int height = 0;

		if (SDL_GetWindowSize(impl->Window, &width, &height))
		{
			size.Width = static_cast<uint32_t>(std::max(width, 0));
			size.Height = static_cast<uint32_t>(std::max(height, 0));
		}

		return size;
	}

	Extent2D Window::GetPixelSize() const
	{
		Extent2D size{};

		if (!impl || !impl->Window)
		{
			return size;
		}

		int width = 0;
		int height = 0;

		if (SDL_GetWindowSizeInPixels(impl->Window, &width, &height))
		{
			size.Width = static_cast<uint32_t>(std::max(width, 0));
			size.Height = static_cast<uint32_t>(std::max(height, 0));
		}

		return size;
	}

	float Window::GetDpiScale() const
	{
		return impl && impl->Window ? SDL_GetWindowDisplayScale(impl->Window) : 1.0f;
	}

	bool Window::IsFocused() const
	{
		return impl && impl->Window && (SDL_GetWindowFlags(impl->Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
	}

	bool Window::IsMinimized() const
	{
		return impl && impl->Window && (SDL_GetWindowFlags(impl->Window) & SDL_WINDOW_MINIMIZED) != 0;
	}

	bool Window::IsExternal() const
	{
		return impl && (impl->ExternalWindow || impl->ExternalParent);
	}

	NativeWindowHandle Window::GetNativeHandle() const
	{
		return impl ? QueryNativeHandle(impl->Window) : NativeWindowHandle{};
	}

	void Window::SetTitle(const std::string& title)
	{
		if (impl && impl->Window)
		{
			SDL_SetWindowTitle(impl->Window, title.c_str());
		}
	}

	void Window::SetSize(Extent2D size)
	{
		if (impl && impl->Window && size.Width > 0 && size.Height > 0)
		{
			SDL_SetWindowSize(impl->Window, static_cast<int>(size.Width), static_cast<int>(size.Height));
		}
	}

	void Window::SyncExternalParentSize()
	{
		if (!impl || !impl->Window || !impl->ExternalParent || !impl->Parent.IsValid())
		{
			return;
		}

	#if defined(_WIN32)

		if (impl->Parent.Type == NativeWindowType::Win32)
		{
			const NativeWindowHandle native = QueryNativeHandle(impl->Window);
			HWND hwnd = static_cast<HWND>(native.Window);
			HWND parentHwnd = static_cast<HWND>(impl->Parent.Window);
			RECT rect{};

			if (hwnd && parentHwnd && GetClientRect(parentHwnd, &rect))
			{
				const int width = std::max(1L, rect.right - rect.left);
				const int height = std::max(1L, rect.bottom - rect.top);
				SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}

	#endif
	}

	void Window::Show()
	{
		if (impl && impl->Window)
		{
			// The hints are global and read when the window is shown: apply this window's own choice.
			SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, impl->Activate ? "1" : "0");
			SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, impl->Activate ? "1" : "0");
			SDL_ShowWindow(impl->Window);
		}
	}

	bool Window::Minimize()
	{
		return impl && impl->Window && SDL_MinimizeWindow(impl->Window);
	}

	bool Window::Restore()
	{
		if (!impl || !impl->Window)
		{
			return false;
		}

	#if defined(_WIN32)
		// SDL restores with SW_RESTORE, which activates the window.
		if (!impl->Activate && (SDL_GetWindowFlags(impl->Window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
		{
			if (HWND hwnd = static_cast<HWND>(QueryNativeHandle(impl->Window).Window))
			{
				ShowWindow(hwnd, SW_SHOWNOACTIVATE);
				return true;
			}
		}
	#endif

		return SDL_RestoreWindow(impl->Window);
	}

	bool Window::SetMode(WindowMode mode, const DisplayModeDesc* fullscreenMode)
	{
		if (!impl || !impl->Window || IsExternal())
		{
			return false;
		}

		SDL_Window* window = impl->Window;

		if (mode == WindowMode::Windowed)
		{
			SDL_SetWindowFullscreen(window, false);
			return true;
		}

		if (mode == WindowMode::ExclusiveFullscreen)
		{
			SDL_DisplayID display = SDL_GetDisplayForWindow(window);
			display = display ? display : SDL_GetPrimaryDisplay();
			SDL_DisplayMode closest{};
			const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
			const bool wantsDesktop = !fullscreenMode || fullscreenMode->Width == 0 || fullscreenMode->Height == 0;
			const int width = wantsDesktop ? (desktop ? desktop->w : 0) : static_cast<int>(fullscreenMode->Width);
			const int height = wantsDesktop ? (desktop ? desktop->h : 0) : static_cast<int>(fullscreenMode->Height);
			const float refresh = wantsDesktop ? (desktop ? desktop->refresh_rate : 0.0f) : fullscreenMode->RefreshRate;

			if (width > 0 && height > 0 && SDL_GetClosestFullscreenDisplayMode(display, width, height, refresh, true, &closest) &&
				SDL_SetWindowFullscreenMode(window, &closest) && SDL_SetWindowFullscreen(window, true))
			{
				return true;
			}

			std::cerr << "[Platform] Exclusive fullscreen is unavailable (" << SDL_GetError() << "); using borderless.\n";
		}

		SDL_SetWindowFullscreenMode(window, nullptr);
		const bool ok = SDL_SetWindowFullscreen(window, true);
		return ok && mode == WindowMode::Borderless;
	}

	WindowMode Window::GetMode() const
	{
		if (!impl || !impl->Window || (SDL_GetWindowFlags(impl->Window) & SDL_WINDOW_FULLSCREEN) == 0)
		{
			return WindowMode::Windowed;
		}

		return SDL_GetWindowFullscreenMode(impl->Window) ? WindowMode::ExclusiveFullscreen : WindowMode::Borderless;
	}

	uint32_t Window::GetDisplay() const
	{
		return impl && impl->Window ? static_cast<uint32_t>(SDL_GetDisplayForWindow(impl->Window)) : 0;
	}

	bool Window::MoveToDisplay(uint32_t displayId)
	{
		if (!impl || !impl->Window || IsExternal())
		{
			return false;
		}

		const SDL_DisplayID display = displayId ? static_cast<SDL_DisplayID>(displayId) : SDL_GetPrimaryDisplay();

		if (display == SDL_GetDisplayForWindow(impl->Window))
		{
			return true;
		}

		const WindowMode mode = GetMode();

		if (mode != WindowMode::Windowed)
		{
			SDL_SetWindowFullscreen(impl->Window, false);
		}

		const bool moved = SDL_SetWindowPosition(impl->Window, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
			static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
		SDL_SyncWindow(impl->Window);

		if (mode != WindowMode::Windowed)
		{
			SetMode(mode);
		}

		return moved;
	}

	void Window::SetCursorVisible(bool visible)
	{
		if (visible)
		{
			SDL_ShowCursor();
		}
		else
		{
			SDL_HideCursor();
		}
	}

	void Window::SetCursorConfined(bool confined)
	{
		if (impl && impl->Window && (impl->CaptureMouse || !confined))
		{
			SDL_SetWindowMouseGrab(impl->Window, confined);
		}
	}

	bool Window::SetRelativeMouseMode(bool enabled)
	{
		if (!impl || !impl->Window || (enabled && !impl->CaptureMouse))
		{
			return false;
		}

		SDL_SetBooleanProperty(SDL_GetWindowProperties(impl->Window), Internal::RelativeMouseRequestedProperty, enabled);

		// Applied now only while focused; WindowSystem re-applies it on focus changes.
		if (enabled && !IsFocused())
		{
			return true;
		}

		return SDL_SetWindowRelativeMouseMode(impl->Window, enabled);
	}

	bool Window::GetMousePosition(float& x, float& y) const
	{
		if (!impl || !impl->Window || SDL_GetMouseFocus() != impl->Window)
		{
			return false;
		}

		SDL_GetMouseState(&x, &y);
		return true;
	}

	void Window::WarpMouse(float x, float y)
	{
		if (impl && impl->Window && impl->CaptureMouse && !SDL_GetWindowRelativeMouseMode(impl->Window))
		{
			SDL_WarpMouseInWindow(impl->Window, x, y);
		}
	}

	bool Window::IsRelativeMouseModeRequested() const
	{
		return impl && impl->Window &&
			SDL_GetBooleanProperty(SDL_GetWindowProperties(impl->Window), Internal::RelativeMouseRequestedProperty, false);
	}

}
