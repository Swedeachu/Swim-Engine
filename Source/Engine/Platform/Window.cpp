#include "Window.h"
#include "Internal/WindowInternal.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <algorithm>
#include <cstdint>
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
			SDL_ShowWindow(impl->Window);
		}
	}

	bool Window::Minimize()
	{
		return impl && impl->Window && SDL_MinimizeWindow(impl->Window);
	}

	bool Window::Restore()
	{
		return impl && impl->Window && SDL_RestoreWindow(impl->Window);
	}

}
