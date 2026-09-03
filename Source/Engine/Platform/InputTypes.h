#pragma once

#include "PlatformTypes.h"
#include <cstdint>
#include <string>

namespace Swim::Platform
{

	enum class KeyCode : uint16_t
	{
		Unknown,
		A, B, C, D, E, F, G, H, I, J, K, L, M,
		N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4,
		Num5, Num6, Num7, Num8, Num9,
		Escape,
		Tab,
		Enter,
		Backspace,
		Space,
		LeftShift,
		RightShift,
		LeftControl,
		RightControl,
		LeftAlt,
		RightAlt,
		LeftSuper,
		RightSuper,
		Up,
		Down,
		Left,
		Right,
		PageUp,
		PageDown,
		Home,
		End,
		Insert,
		Delete,
		F1, F2, F3, F4, F5, F6,
		F7, F8, F9, F10, F11, F12,
		Count
	};

	enum class ScanCode : uint16_t
	{
		Unknown,
		A, B, C, D, E, F, G, H, I, J, K, L, M,
		N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4,
		Num5, Num6, Num7, Num8, Num9,
		Escape,
		Tab,
		Enter,
		Backspace,
		Space,
		LeftShift,
		RightShift,
		LeftControl,
		RightControl,
		LeftAlt,
		RightAlt,
		Up,
		Down,
		Left,
		Right,
		PageUp,
		PageDown,
		Home,
		End,
		Insert,
		Delete,
		F1, F2, F3, F4, F5, F6,
		F7, F8, F9, F10, F11, F12,
		Count
	};

	enum class MouseButton : uint8_t
	{
		Unknown,
		Left,
		Middle,
		Right,
		X1,
		X2,
		Count
	};

	enum class GamepadButton : uint8_t
	{
		Unknown,
		South,
		East,
		West,
		North,
		Back,
		Guide,
		Start,
		LeftStick,
		RightStick,
		LeftShoulder,
		RightShoulder,
		DpadUp,
		DpadDown,
		DpadLeft,
		DpadRight,
		Count
	};

	enum class GamepadAxis : uint8_t
	{
		Unknown,
		LeftX,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger,
		Count
	};

	enum class InputEventType : uint8_t
	{
		KeyDown,
		KeyUp,
		MouseButtonDown,
		MouseButtonUp,
		MouseMove,
		MouseWheel,
		TextInput,
		TextEditing,
		GamepadAdded,
		GamepadRemoved,
		GamepadButtonDown,
		GamepadButtonUp,
		GamepadAxisMotion
	};


	struct InputEvent
	{
		InputEventType Type = InputEventType::KeyDown;
		WindowId Window = 0;
		InputDeviceId Device = 0;
		KeyCode Key = KeyCode::Unknown;
		ScanCode PhysicalKey = ScanCode::Unknown;
		MouseButton Mouse = MouseButton::Unknown;
		GamepadButton Gamepad = GamepadButton::Unknown;
		GamepadAxis Axis = GamepadAxis::Unknown;
		Float2 Position{};
		Float2 Delta{};
		float AxisValue = 0.0f;
		bool Repeat = false;
		std::string Text;
		int32_t EditStart = 0;
		int32_t EditLength = 0;
	};

}
