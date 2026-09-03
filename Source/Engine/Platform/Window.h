#pragma once

#include "PlatformTypes.h"
#include <memory>
#include <string>

namespace Swim::Platform
{

	enum class WindowGraphicsSupport : uint8_t
	{
		None,
		Vulkan,
		OpenGL
	};

	struct WindowDesc
	{
		std::string Title = "Swim";
		uint32_t Width = 1280;
		uint32_t Height = 720;
		bool Resizable = true;
		bool HighPixelDensity = true;
		bool Hidden = false;
		WindowGraphicsSupport GraphicsSupport = WindowGraphicsSupport::None;
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

	private:

		struct Impl;
		explicit Window(std::unique_ptr<Impl> impl);

		std::unique_ptr<Impl> impl;

		friend class WindowSystem;
	};

}
