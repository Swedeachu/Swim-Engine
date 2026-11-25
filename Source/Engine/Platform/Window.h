#pragma once

#include "PlatformTypes.h"
#include <memory>
#include <string>

namespace Swim::Platform
{

	namespace Internal
	{

		struct WindowAccess;

	}

	enum class WindowGraphicsSupport : uint8_t
	{
		None,
		Vulkan
	};

	// Borderless is a desktop-sized window over the whole display (no video mode change);
	// ExclusiveFullscreen switches the display's video mode where the platform allows it.
	enum class WindowMode : uint8_t
	{
		Windowed,
		Borderless,
		ExclusiveFullscreen
	};

	struct WindowDesc
	{
		std::string Title = "Swim";
		uint32_t Width = 1280;
		uint32_t Height = 720;
		bool Resizable = true;
		bool HighPixelDensity = true;
		bool Hidden = false;
		// Windowed mode: open maximized, filling the work area (title bar and task bar stay visible).
		bool Maximized = false;
		WindowGraphicsSupport GraphicsSupport = WindowGraphicsSupport::None;
		WindowMode Mode = WindowMode::Windowed;
		// Display to open on (DisplayInfo::Id); 0 = the primary display.
		uint32_t Display = 0;
		// False: the window opens without taking focus from the active application.
		bool Activate = true;
		// False: relative mouse mode, cursor confinement and pointer warps are ignored.
		bool CaptureMouse = true;
		// 1-based position in the system's display list; when set it overrides Display.
		// Out of range falls back to Display.
		uint32_t DisplayIndex = 0;
		// Video mode for ExclusiveFullscreen; zero size = the display's desktop mode.
		DisplayModeDesc FullscreenMode{};
		NativeWindowHandle ExternalWindow{};
		NativeWindowHandle ExternalParent{};
	};

	class Window
	{

	public:

		Window(Window&&) noexcept;

		Window& operator=(Window&&) noexcept;

		~Window();

		WindowId GetId() const;

		Extent2D GetLogicalSize() const;

		Extent2D GetPixelSize() const;

		float GetDpiScale() const;

		bool IsFocused() const;

		bool IsMinimized() const;

		bool IsExternal() const;

		NativeWindowHandle GetNativeHandle() const;

		void SetTitle(const std::string& title);

		void SetSize(Extent2D size);

		void SyncExternalParentSize();

		void Show();

		// Requests are asynchronous: observe IsMinimized()/window events before
		// assuming the window manager has completed either operation.
		bool Minimize();

		bool Restore();

		// Switches the window mode. For ExclusiveFullscreen, `mode` picks the closest video
		// mode the display offers (null or zero size = the desktop mode). Falls back to
		// Borderless and returns false when exclusive fullscreen is unavailable. The
		// swapchain follows through the usual PixelSizeChanged event.
		bool SetMode(WindowMode mode, const DisplayModeDesc* fullscreenMode = nullptr);

		WindowMode GetMode() const;

		// The display holding most of the window (DisplayInfo::Id), 0 if unknown.
		uint32_t GetDisplay() const;

		// Moves the window to a display (centered when windowed) and keeps its mode.
		bool MoveToDisplay(uint32_t displayId);

		void SetCursorVisible(bool visible);

		// Keeps the pointer inside the window while it has focus.
		void SetCursorConfined(bool confined);

		// Hides the cursor and reports unaccelerated motion deltas (InputEvent::Delta) for
		// camera look. Released while the window is unfocused and restored when focus
		// returns, as long as it stays requested.
		bool SetRelativeMouseMode(bool enabled);

		bool IsRelativeMouseModeRequested() const;

		// The pointer in logical window coordinates (GetLogicalSize); false when it is not over
		// this window or the window has no mouse focus.
		bool GetMousePosition(float& x, float& y) const;

		// Moves the pointer to logical window coordinates. Ignored in relative mouse mode and
		// without CaptureMouse.
		void WarpMouse(float x, float y);

	private:

		struct Impl;
		explicit Window(std::unique_ptr<Impl> impl);

		std::unique_ptr<Impl> impl;

		friend class WindowSystem;
		friend struct Internal::WindowAccess;

	};

}
