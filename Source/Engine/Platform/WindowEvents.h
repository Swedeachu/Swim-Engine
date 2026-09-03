#pragma once

#include "PlatformTypes.h"
#include <cstdint>

namespace Swim::Platform
{

	enum class WindowEventType : uint8_t
	{
		CloseRequested,
		Shown,
		Hidden,
		Moved,
		Resized,
		PixelSizeChanged,
		Minimized,
		Restored,
		Maximized,
		FocusGained,
		FocusLost,
		MouseEntered,
		MouseLeft,
		DisplayChanged,
		DpiScaleChanged
	};

	struct WindowEvent
	{
		WindowEventType Type = WindowEventType::Shown;
		WindowId Window = 0;
		int32_t X = 0;
		int32_t Y = 0;
		Extent2D LogicalSize{};
		Extent2D PixelSize{};
		float DpiScale = 1.0f;
	};

}
