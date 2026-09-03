#pragma once

#include <cstdint>
#include <string>

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

	struct DisplayInfo
	{
		uint32_t Id = 0;
		std::string Name;
		int32_t X = 0;
		int32_t Y = 0;
		Extent2D Size{};
		float ContentScale = 1.0f;
	};

}
