#include "WindowSystem.h"
#include "Internal/WindowInternal.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <unordered_map>

#if defined(_WIN32)
	#include "Internal/WindowsApi.h"
#endif

namespace Swim::Platform
{

	struct WindowSystem::Impl
	{
		std::unordered_map<SDL_JoystickID, SDL_Gamepad*> Gamepads;

		~Impl()
		{
			for (auto& [id, gamepad] : Gamepads)
			{
				if (gamepad)
				{
					SDL_CloseGamepad(gamepad);
				}
			}
		}
	};

	namespace
	{

		KeyCode ToKeyCode(SDL_Keycode key)
		{
			switch (key)
			{
				case SDLK_A: return KeyCode::A;
				case SDLK_B: return KeyCode::B;
				case SDLK_C: return KeyCode::C;
				case SDLK_D: return KeyCode::D;
				case SDLK_E: return KeyCode::E;
				case SDLK_F: return KeyCode::F;
				case SDLK_G: return KeyCode::G;
				case SDLK_H: return KeyCode::H;
				case SDLK_I: return KeyCode::I;
				case SDLK_J: return KeyCode::J;
				case SDLK_K: return KeyCode::K;
				case SDLK_L: return KeyCode::L;
				case SDLK_M: return KeyCode::M;
				case SDLK_N: return KeyCode::N;
				case SDLK_O: return KeyCode::O;
				case SDLK_P: return KeyCode::P;
				case SDLK_Q: return KeyCode::Q;
				case SDLK_R: return KeyCode::R;
				case SDLK_S: return KeyCode::S;
				case SDLK_T: return KeyCode::T;
				case SDLK_U: return KeyCode::U;
				case SDLK_V: return KeyCode::V;
				case SDLK_W: return KeyCode::W;
				case SDLK_X: return KeyCode::X;
				case SDLK_Y: return KeyCode::Y;
				case SDLK_Z: return KeyCode::Z;
				case SDLK_0: return KeyCode::Num0;
				case SDLK_1: return KeyCode::Num1;
				case SDLK_2: return KeyCode::Num2;
				case SDLK_3: return KeyCode::Num3;
				case SDLK_4: return KeyCode::Num4;
				case SDLK_5: return KeyCode::Num5;
				case SDLK_6: return KeyCode::Num6;
				case SDLK_7: return KeyCode::Num7;
				case SDLK_8: return KeyCode::Num8;
				case SDLK_9: return KeyCode::Num9;
				case SDLK_ESCAPE: return KeyCode::Escape;
				case SDLK_TAB: return KeyCode::Tab;
				case SDLK_RETURN: return KeyCode::Enter;
				case SDLK_BACKSPACE: return KeyCode::Backspace;
				case SDLK_SPACE: return KeyCode::Space;
				case SDLK_LSHIFT: return KeyCode::LeftShift;
				case SDLK_RSHIFT: return KeyCode::RightShift;
				case SDLK_LCTRL: return KeyCode::LeftControl;
				case SDLK_RCTRL: return KeyCode::RightControl;
				case SDLK_LALT: return KeyCode::LeftAlt;
				case SDLK_RALT: return KeyCode::RightAlt;
				case SDLK_LGUI: return KeyCode::LeftSuper;
				case SDLK_RGUI: return KeyCode::RightSuper;
				case SDLK_UP: return KeyCode::Up;
				case SDLK_DOWN: return KeyCode::Down;
				case SDLK_LEFT: return KeyCode::Left;
				case SDLK_RIGHT: return KeyCode::Right;
				case SDLK_PAGEUP: return KeyCode::PageUp;
				case SDLK_PAGEDOWN: return KeyCode::PageDown;
				case SDLK_HOME: return KeyCode::Home;
				case SDLK_END: return KeyCode::End;
				case SDLK_INSERT: return KeyCode::Insert;
				case SDLK_DELETE: return KeyCode::Delete;
				case SDLK_F1: return KeyCode::F1;
				case SDLK_F2: return KeyCode::F2;
				case SDLK_F3: return KeyCode::F3;
				case SDLK_F4: return KeyCode::F4;
				case SDLK_F5: return KeyCode::F5;
				case SDLK_F6: return KeyCode::F6;
				case SDLK_F7: return KeyCode::F7;
				case SDLK_F8: return KeyCode::F8;
				case SDLK_F9: return KeyCode::F9;
				case SDLK_F10: return KeyCode::F10;
				case SDLK_F11: return KeyCode::F11;
				case SDLK_F12: return KeyCode::F12;
				default: return KeyCode::Unknown;
			}
		}

		ScanCode ToScanCode(SDL_Scancode scanCode)
		{
			switch (scanCode)
			{
				case SDL_SCANCODE_A: return ScanCode::A;
				case SDL_SCANCODE_B: return ScanCode::B;
				case SDL_SCANCODE_C: return ScanCode::C;
				case SDL_SCANCODE_D: return ScanCode::D;
				case SDL_SCANCODE_E: return ScanCode::E;
				case SDL_SCANCODE_F: return ScanCode::F;
				case SDL_SCANCODE_G: return ScanCode::G;
				case SDL_SCANCODE_H: return ScanCode::H;
				case SDL_SCANCODE_I: return ScanCode::I;
				case SDL_SCANCODE_J: return ScanCode::J;
				case SDL_SCANCODE_K: return ScanCode::K;
				case SDL_SCANCODE_L: return ScanCode::L;
				case SDL_SCANCODE_M: return ScanCode::M;
				case SDL_SCANCODE_N: return ScanCode::N;
				case SDL_SCANCODE_O: return ScanCode::O;
				case SDL_SCANCODE_P: return ScanCode::P;
				case SDL_SCANCODE_Q: return ScanCode::Q;
				case SDL_SCANCODE_R: return ScanCode::R;
				case SDL_SCANCODE_S: return ScanCode::S;
				case SDL_SCANCODE_T: return ScanCode::T;
				case SDL_SCANCODE_U: return ScanCode::U;
				case SDL_SCANCODE_V: return ScanCode::V;
				case SDL_SCANCODE_W: return ScanCode::W;
				case SDL_SCANCODE_X: return ScanCode::X;
				case SDL_SCANCODE_Y: return ScanCode::Y;
				case SDL_SCANCODE_Z: return ScanCode::Z;
				case SDL_SCANCODE_0: return ScanCode::Num0;
				case SDL_SCANCODE_1: return ScanCode::Num1;
				case SDL_SCANCODE_2: return ScanCode::Num2;
				case SDL_SCANCODE_3: return ScanCode::Num3;
				case SDL_SCANCODE_4: return ScanCode::Num4;
				case SDL_SCANCODE_5: return ScanCode::Num5;
				case SDL_SCANCODE_6: return ScanCode::Num6;
				case SDL_SCANCODE_7: return ScanCode::Num7;
				case SDL_SCANCODE_8: return ScanCode::Num8;
				case SDL_SCANCODE_9: return ScanCode::Num9;
				case SDL_SCANCODE_ESCAPE: return ScanCode::Escape;
				case SDL_SCANCODE_TAB: return ScanCode::Tab;
				case SDL_SCANCODE_RETURN: return ScanCode::Enter;
				case SDL_SCANCODE_BACKSPACE: return ScanCode::Backspace;
				case SDL_SCANCODE_SPACE: return ScanCode::Space;
				case SDL_SCANCODE_LSHIFT: return ScanCode::LeftShift;
				case SDL_SCANCODE_RSHIFT: return ScanCode::RightShift;
				case SDL_SCANCODE_LCTRL: return ScanCode::LeftControl;
				case SDL_SCANCODE_RCTRL: return ScanCode::RightControl;
				case SDL_SCANCODE_LALT: return ScanCode::LeftAlt;
				case SDL_SCANCODE_RALT: return ScanCode::RightAlt;
				case SDL_SCANCODE_UP: return ScanCode::Up;
				case SDL_SCANCODE_DOWN: return ScanCode::Down;
				case SDL_SCANCODE_LEFT: return ScanCode::Left;
				case SDL_SCANCODE_RIGHT: return ScanCode::Right;
				case SDL_SCANCODE_PAGEUP: return ScanCode::PageUp;
				case SDL_SCANCODE_PAGEDOWN: return ScanCode::PageDown;
				case SDL_SCANCODE_HOME: return ScanCode::Home;
				case SDL_SCANCODE_END: return ScanCode::End;
				case SDL_SCANCODE_INSERT: return ScanCode::Insert;
				case SDL_SCANCODE_DELETE: return ScanCode::Delete;
				case SDL_SCANCODE_F1: return ScanCode::F1;
				case SDL_SCANCODE_F2: return ScanCode::F2;
				case SDL_SCANCODE_F3: return ScanCode::F3;
				case SDL_SCANCODE_F4: return ScanCode::F4;
				case SDL_SCANCODE_F5: return ScanCode::F5;
				case SDL_SCANCODE_F6: return ScanCode::F6;
				case SDL_SCANCODE_F7: return ScanCode::F7;
				case SDL_SCANCODE_F8: return ScanCode::F8;
				case SDL_SCANCODE_F9: return ScanCode::F9;
				case SDL_SCANCODE_F10: return ScanCode::F10;
				case SDL_SCANCODE_F11: return ScanCode::F11;
				case SDL_SCANCODE_F12: return ScanCode::F12;
				default: return ScanCode::Unknown;
			}
		}

		MouseButton ToMouseButton(uint8_t button)
		{
			switch (button)
			{
				case SDL_BUTTON_LEFT: return MouseButton::Left;
				case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
				case SDL_BUTTON_RIGHT: return MouseButton::Right;
				case SDL_BUTTON_X1: return MouseButton::X1;
				case SDL_BUTTON_X2: return MouseButton::X2;
				default: return MouseButton::Unknown;
			}
		}

		GamepadButton ToGamepadButton(SDL_GamepadButton button)
		{
			switch (button)
			{
				case SDL_GAMEPAD_BUTTON_SOUTH: return GamepadButton::South;
				case SDL_GAMEPAD_BUTTON_EAST: return GamepadButton::East;
				case SDL_GAMEPAD_BUTTON_WEST: return GamepadButton::West;
				case SDL_GAMEPAD_BUTTON_NORTH: return GamepadButton::North;
				case SDL_GAMEPAD_BUTTON_BACK: return GamepadButton::Back;
				case SDL_GAMEPAD_BUTTON_GUIDE: return GamepadButton::Guide;
				case SDL_GAMEPAD_BUTTON_START: return GamepadButton::Start;
				case SDL_GAMEPAD_BUTTON_LEFT_STICK: return GamepadButton::LeftStick;
				case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return GamepadButton::RightStick;
				case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GamepadButton::LeftShoulder;
				case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadButton::RightShoulder;
				case SDL_GAMEPAD_BUTTON_DPAD_UP: return GamepadButton::DpadUp;
				case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GamepadButton::DpadDown;
				case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GamepadButton::DpadLeft;
				case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GamepadButton::DpadRight;
				default: return GamepadButton::Unknown;
			}
		}

		GamepadAxis ToGamepadAxis(SDL_GamepadAxis axis)
		{
			switch (axis)
			{
				case SDL_GAMEPAD_AXIS_LEFTX: return GamepadAxis::LeftX;
				case SDL_GAMEPAD_AXIS_LEFTY: return GamepadAxis::LeftY;
				case SDL_GAMEPAD_AXIS_RIGHTX: return GamepadAxis::RightX;
				case SDL_GAMEPAD_AXIS_RIGHTY: return GamepadAxis::RightY;
				case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return GamepadAxis::LeftTrigger;
				case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return GamepadAxis::RightTrigger;
				default: return GamepadAxis::Unknown;
			}
		}

		WindowEvent MakeWindowEvent(WindowEventType type, SDL_WindowID windowId, int32_t x = 0, int32_t y = 0)
		{
			WindowEvent event{};
			event.Type = type;
			event.Window = static_cast<WindowId>(windowId);
			event.X = x;
			event.Y = y;

			SDL_Window* window = SDL_GetWindowFromID(windowId);
			if (window)
			{
				int width = 0;
				int height = 0;
				if (SDL_GetWindowSize(window, &width, &height))
				{
					event.LogicalSize = {
						static_cast<uint32_t>(std::max(width, 0)),
						static_cast<uint32_t>(std::max(height, 0))
					};
				}

				if (SDL_GetWindowSizeInPixels(window, &width, &height))
				{
					event.PixelSize = {
						static_cast<uint32_t>(std::max(width, 0)),
						static_cast<uint32_t>(std::max(height, 0))
					};
				}

				event.DpiScale = SDL_GetWindowDisplayScale(window);
			}

			return event;
		}

	#if defined(_WIN32)
		struct ExternalParentAttachment
		{
			bool Success = false;
			uint32_t ParentThreadId = 0;
			uint32_t ForegroundThreadId = 0;
			std::uintptr_t OriginalWindowProc = 0;
		};

		constexpr wchar_t EmbeddedOriginalWindowProcProperty[] = L"SwimEngine.EmbeddedOriginalWindowProc";

		LRESULT CALLBACK EmbeddedChildWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			WNDPROC originalWindowProc = reinterpret_cast<WNDPROC>(GetPropW(hwnd, EmbeddedOriginalWindowProcProperty));
			if (!originalWindowProc)
			{
				return DefWindowProcW(hwnd, message, wParam, lParam);
			}

			switch (message)
			{
				case WM_GETDLGCODE:
					return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;

				case WM_LBUTTONDOWN:
				case WM_RBUTTONDOWN:
				case WM_MBUTTONDOWN:
				case WM_XBUTTONDOWN:
					SetFocus(hwnd);
					break;

				default:
					break;
			}

			return CallWindowProcW(originalWindowProc, hwnd, message, wParam, lParam);
		}

		ExternalParentAttachment AttachToExternalParent(SDL_Window* window, const NativeWindowHandle& parent)
		{
			ExternalParentAttachment result{};
			if (!window || parent.Type != NativeWindowType::Win32 || !parent.Window)
			{
				return result;
			}

			const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
			HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
			HWND parentHwnd = static_cast<HWND>(parent.Window);
			if (!hwnd || !parentHwnd)
			{
				return result;
			}

			SetLastError(ERROR_SUCCESS);
			HWND previousParent = SetParent(hwnd, parentHwnd);
			if (!previousParent && GetLastError() != ERROR_SUCCESS)
			{
				return result;
			}

			const DWORD currentThreadId = GetCurrentThreadId();
			auto detachInputThreads = [&]()
			{
				if (result.ForegroundThreadId != 0)
				{
					AttachThreadInput(currentThreadId, result.ForegroundThreadId, FALSE);
					result.ForegroundThreadId = 0;
				}
				if (result.ParentThreadId != 0)
				{
					AttachThreadInput(currentThreadId, result.ParentThreadId, FALSE);
					result.ParentThreadId = 0;
				}
			};

			const DWORD parentThreadId = GetWindowThreadProcessId(parentHwnd, nullptr);
			if (parentThreadId != 0 && parentThreadId != currentThreadId &&
				AttachThreadInput(currentThreadId, parentThreadId, TRUE))
			{
				result.ParentThreadId = parentThreadId;
			}

			if (HWND foreground = GetForegroundWindow())
			{
				const DWORD foregroundThreadId = GetWindowThreadProcessId(foreground, nullptr);
				if (foregroundThreadId != 0 && foregroundThreadId != currentThreadId &&
					foregroundThreadId != parentThreadId &&
					AttachThreadInput(currentThreadId, foregroundThreadId, TRUE))
				{
					result.ForegroundThreadId = foregroundThreadId;
				}
			}

			LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
			style &= ~(WS_OVERLAPPEDWINDOW | WS_POPUP);
			style |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP;
			SetWindowLongPtrW(hwnd, GWL_STYLE, style);

			SetLastError(ERROR_SUCCESS);
			LONG_PTR originalWindowProc = SetWindowLongPtrW(
				hwnd,
				GWLP_WNDPROC,
				reinterpret_cast<LONG_PTR>(EmbeddedChildWindowProc)
			);
			if (originalWindowProc == 0 && GetLastError() != ERROR_SUCCESS)
			{
				detachInputThreads();
				return result;
			}

			if (!SetPropW(hwnd, EmbeddedOriginalWindowProcProperty, reinterpret_cast<HANDLE>(originalWindowProc)))
			{
				SetWindowLongPtrW(hwnd, GWLP_WNDPROC, originalWindowProc);
				detachInputThreads();
				return result;
			}
			result.OriginalWindowProc = static_cast<std::uintptr_t>(originalWindowProc);

			RECT rect{};
			if (!GetClientRect(parentHwnd, &rect))
			{
				RemovePropW(hwnd, EmbeddedOriginalWindowProcProperty);
				SetWindowLongPtrW(hwnd, GWLP_WNDPROC, originalWindowProc);
				detachInputThreads();
				return result;
			}

			const int width = std::max(1L, rect.right - rect.left);
			const int height = std::max(1L, rect.bottom - rect.top);
			SetWindowPos(hwnd, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
			SetFocus(hwnd);
			result.Success = true;
			return result;
		}
	#endif

		bool SetExternalWindowProperty(SDL_PropertiesID properties, const NativeWindowHandle& handle)
		{
			if (!handle.IsValid())
			{
				return true;
			}

			switch (handle.Type)
			{
			#if defined(_WIN32)
				case NativeWindowType::Win32:
					return SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, handle.Window);
			#endif
			#if defined(__APPLE__)
				case NativeWindowType::Cocoa:
					return SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER, handle.Window);
			#endif
			#if defined(__linux__)
				case NativeWindowType::Wayland:
					return SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_WAYLAND_WL_SURFACE_POINTER, handle.Window);
				case NativeWindowType::X11:
					return SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,
						static_cast<Sint64>(reinterpret_cast<std::uintptr_t>(handle.Window)));
			#endif
				default:
					return false;
			}
		}

	}

	WindowSystem::WindowSystem() : impl(std::make_unique<Impl>()) {}
	WindowSystem::~WindowSystem() = default;

	std::unique_ptr<Window> WindowSystem::Create(const WindowDesc& desc)
	{
		SDL_PropertiesID properties = SDL_CreateProperties();
		if (properties == 0)
		{
			std::cerr << "Failed to allocate SDL window properties: " << SDL_GetError() << '\n';
			return nullptr;
		}

		SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, desc.Title.c_str());
		SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, desc.Width);
		SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, desc.Height);
		SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, desc.Resizable);
		SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, desc.HighPixelDensity);
		SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, desc.Hidden);

		if (desc.GraphicsSupport == WindowGraphicsSupport::Vulkan)
		{
			SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
		}
		else if (desc.GraphicsSupport == WindowGraphicsSupport::OpenGL)
		{
			SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
			SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
		}

		if (!SetExternalWindowProperty(properties, desc.ExternalWindow))
		{
			std::cerr << "The requested external native window type is not supported on this platform.\n";
			SDL_DestroyProperties(properties);
			return nullptr;
		}

		SDL_Window* sdlWindow = SDL_CreateWindowWithProperties(properties);
		SDL_DestroyProperties(properties);

		if (!sdlWindow)
		{
			std::cerr << "Failed to create SDL3 window: " << SDL_GetError() << '\n';
			return nullptr;
		}

		auto windowImpl = std::make_unique<Window::Impl>();
		windowImpl->Window = sdlWindow;
		windowImpl->ExternalWindow = desc.ExternalWindow.IsValid();

		if (desc.ExternalParent.IsValid())
		{
		#if defined(_WIN32)
			const ExternalParentAttachment attachment = AttachToExternalParent(sdlWindow, desc.ExternalParent);
			if (desc.ExternalParent.Type != NativeWindowType::Win32 || !attachment.Success)
			{
				std::cerr << "Failed to attach SDL3 window to the external Win32 parent.\n";
				SDL_DestroyWindow(sdlWindow);
				return nullptr;
			}
			windowImpl->ExternalParent = true;
			windowImpl->Parent = desc.ExternalParent;
			windowImpl->ParentThreadId = attachment.ParentThreadId;
			windowImpl->ForegroundThreadId = attachment.ForegroundThreadId;
			windowImpl->OriginalWindowProc = attachment.OriginalWindowProc;
		#else
			std::cerr << "External parent windows are not implemented on this platform yet.\n";
			SDL_DestroyWindow(sdlWindow);
			return nullptr;
		#endif
		}

		return std::unique_ptr<Window>(new Window(std::move(windowImpl)));
	}

	void WindowSystem::PumpEvents(const WindowEventHandler& windowHandler, const InputEventHandler& inputHandler)
	{
		SDL_Event event{};
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_EVENT_QUIT:
				{
					WindowEvent closeEvent{};
					closeEvent.Type = WindowEventType::CloseRequested;
					if (windowHandler) { windowHandler(closeEvent); }
					break;
				}
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::CloseRequested, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_SHOWN:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Shown, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_HIDDEN:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Hidden, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_MOVED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Moved, event.window.windowID, event.window.data1, event.window.data2)); }
					break;
				case SDL_EVENT_WINDOW_RESIZED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Resized, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::PixelSizeChanged, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_MINIMIZED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Minimized, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_RESTORED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Restored, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_MAXIMIZED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::Maximized, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_FOCUS_GAINED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::FocusGained, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_FOCUS_LOST:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::FocusLost, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_MOUSE_ENTER:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::MouseEntered, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_MOUSE_LEAVE:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::MouseLeft, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::DisplayChanged, event.window.windowID)); }
					break;
				case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
					if (windowHandler) { windowHandler(MakeWindowEvent(WindowEventType::DpiScaleChanged, event.window.windowID)); }
					break;

				case SDL_EVENT_KEY_DOWN:
				case SDL_EVENT_KEY_UP:
				{
					InputEvent input{};
					input.Type = event.type == SDL_EVENT_KEY_DOWN ? InputEventType::KeyDown : InputEventType::KeyUp;
					input.Window = static_cast<WindowId>(event.key.windowID);
					input.Device = static_cast<InputDeviceId>(event.key.which);
					input.Key = ToKeyCode(event.key.key);
					input.PhysicalKey = ToScanCode(event.key.scancode);
					input.Repeat = event.key.repeat;
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
				case SDL_EVENT_MOUSE_BUTTON_UP:
				{
					InputEvent input{};
					input.Type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp;
					input.Window = static_cast<WindowId>(event.button.windowID);
					input.Device = static_cast<InputDeviceId>(event.button.which);
					input.Mouse = ToMouseButton(event.button.button);
					input.Position = { event.button.x, event.button.y };
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_MOUSE_MOTION:
				{
					InputEvent input{};
					input.Type = InputEventType::MouseMove;
					input.Window = static_cast<WindowId>(event.motion.windowID);
					input.Device = static_cast<InputDeviceId>(event.motion.which);
					input.Position = { event.motion.x, event.motion.y };
					input.Delta = { event.motion.xrel, event.motion.yrel };
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_MOUSE_WHEEL:
				{
					InputEvent input{};
					input.Type = InputEventType::MouseWheel;
					input.Window = static_cast<WindowId>(event.wheel.windowID);
					input.Device = static_cast<InputDeviceId>(event.wheel.which);
					const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
					input.Delta = { event.wheel.x * direction, event.wheel.y * direction };
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_TEXT_INPUT:
				{
					InputEvent input{};
					input.Type = InputEventType::TextInput;
					input.Window = static_cast<WindowId>(event.text.windowID);
					if (event.text.text) { input.Text = event.text.text; }
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_TEXT_EDITING:
				{
					InputEvent input{};
					input.Type = InputEventType::TextEditing;
					input.Window = static_cast<WindowId>(event.edit.windowID);
					if (event.edit.text) { input.Text = event.edit.text; }
					input.EditStart = event.edit.start;
					input.EditLength = event.edit.length;
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_GAMEPAD_ADDED:
				{
					const SDL_JoystickID id = event.gdevice.which;
					if (!impl->Gamepads.contains(id))
					{
						if (SDL_Gamepad* gamepad = SDL_OpenGamepad(id))
						{
							impl->Gamepads[id] = gamepad;
						}
					}

					InputEvent input{};
					input.Type = InputEventType::GamepadAdded;
					input.Device = static_cast<InputDeviceId>(id);
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_GAMEPAD_REMOVED:
				{
					const SDL_JoystickID id = event.gdevice.which;
					auto found = impl->Gamepads.find(id);
					if (found != impl->Gamepads.end())
					{
						SDL_CloseGamepad(found->second);
						impl->Gamepads.erase(found);
					}

					InputEvent input{};
					input.Type = InputEventType::GamepadRemoved;
					input.Device = static_cast<InputDeviceId>(id);
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				case SDL_EVENT_GAMEPAD_BUTTON_UP:
				{
					InputEvent input{};
					input.Type = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? InputEventType::GamepadButtonDown : InputEventType::GamepadButtonUp;
					input.Device = static_cast<InputDeviceId>(event.gbutton.which);
					input.Gamepad = ToGamepadButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
					if (inputHandler) { inputHandler(input); }
					break;
				}
				case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				{
					InputEvent input{};
					input.Type = InputEventType::GamepadAxisMotion;
					input.Device = static_cast<InputDeviceId>(event.gaxis.which);
					input.Axis = ToGamepadAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis));
					const bool trigger = input.Axis == GamepadAxis::LeftTrigger || input.Axis == GamepadAxis::RightTrigger;
					input.AxisValue = trigger
						? std::clamp(static_cast<float>(event.gaxis.value) / 32767.0f, 0.0f, 1.0f)
						: std::clamp(static_cast<float>(event.gaxis.value) / 32767.0f, -1.0f, 1.0f);
					if (inputHandler) { inputHandler(input); }
					break;
				}
				default:
					break;
			}
		}
	}

	std::vector<DisplayInfo> WindowSystem::GetDisplays() const
	{
		std::vector<DisplayInfo> result;
		int count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&count);
		if (!displays)
		{
			return result;
		}

		result.reserve(static_cast<size_t>(std::max(count, 0)));
		for (int i = 0; i < count; ++i)
		{
			DisplayInfo info{};
			info.Id = static_cast<uint32_t>(displays[i]);
			if (const char* name = SDL_GetDisplayName(displays[i]))
			{
				info.Name = name;
			}

			SDL_Rect bounds{};
			if (SDL_GetDisplayBounds(displays[i], &bounds))
			{
				info.X = bounds.x;
				info.Y = bounds.y;
				info.Size = {
					static_cast<uint32_t>(std::max(bounds.w, 0)),
					static_cast<uint32_t>(std::max(bounds.h, 0))
				};
			}

			const float contentScale = SDL_GetDisplayContentScale(displays[i]);
			info.ContentScale = contentScale > 0.0f ? contentScale : 1.0f;
			result.push_back(std::move(info));
		}

		SDL_free(displays);
		return result;
	}

	void WindowSystem::StartTextInput(Window& window)
	{
		if (window.impl && window.impl->Window)
		{
			SDL_StartTextInput(window.impl->Window);
		}
	}

	void WindowSystem::StopTextInput(Window& window)
	{
		if (window.impl && window.impl->Window)
		{
			SDL_StopTextInput(window.impl->Window);
		}
	}

	bool WindowSystem::SetGamepadRumble(InputDeviceId device, float lowFrequency, float highFrequency, uint32_t durationMilliseconds)
	{
		auto it = impl->Gamepads.find(static_cast<SDL_JoystickID>(device));
		if (it == impl->Gamepads.end() || !it->second)
		{
			return false;
		}

		const uint16_t low = static_cast<uint16_t>(std::clamp(lowFrequency, 0.0f, 1.0f) * 65535.0f);
		const uint16_t high = static_cast<uint16_t>(std::clamp(highFrequency, 0.0f, 1.0f) * 65535.0f);
		return SDL_RumbleGamepad(it->second, low, high, durationMilliseconds);
	}

}
