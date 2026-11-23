#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Swim::Platform
{

	struct Extent2D
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
	};

	struct Float2
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	using WindowId = uint32_t;
	using InputDeviceId = uint64_t;

	enum class NativeWindowType : uint8_t
	{
		None,
		Win32,
		X11,
		Wayland,
		Cocoa
	};

	struct NativeWindowHandle
	{
		NativeWindowType Type = NativeWindowType::None;
		void* Window = nullptr;
		void* Display = nullptr;

		bool IsValid() const { return Type != NativeWindowType::None && Window != nullptr; }
	};

	// A fullscreen video mode. Width/Height are in pixels; RefreshRate 0 = any.
	struct DisplayModeDesc
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		float RefreshRate = 0.0f;
		float PixelDensity = 1.0f;
	};

	struct DisplayInfo
	{
		uint32_t Id = 0;
		std::string Name;
		int32_t X = 0;
		int32_t Y = 0;
		Extent2D Size{};
		float ContentScale = 1.0f;
		bool Primary = false;
		DisplayModeDesc DesktopMode{};
		// Exclusive fullscreen modes, largest first (empty when the platform offers none).
		std::vector<DisplayModeDesc> Modes;
	};

}
