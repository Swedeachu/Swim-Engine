#include "Engine/Input/InputNames.h"

#include <array>
#include <cctype>

namespace Swim::Input
{

	namespace
	{

		constexpr std::array<std::string_view, static_cast<std::size_t>(Platform::ScanCode::Count)> ScanCodeNames = { "Unknown", "A", "B",
			"C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Num0",
			"Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9", "Escape", "Tab", "Enter", "Backspace", "Space",
			"LeftShift", "RightShift", "LeftControl", "RightControl", "LeftAlt", "RightAlt", "Up", "Down", "Left", "Right", "PageUp",
			"PageDown", "Home", "End", "Insert", "Delete", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"Grave", "Minus", "Equals", "LeftBracket", "RightBracket", "Backslash", "Semicolon", "Apostrophe", "Comma", "Period", "Slash",
			"CapsLock", "LeftSuper", "RightSuper", "Menu", "PrintScreen", "ScrollLock", "Pause", "NumLock", "Keypad0", "Keypad1",
			"Keypad2", "Keypad3", "Keypad4", "Keypad5", "Keypad6", "Keypad7", "Keypad8", "Keypad9", "KeypadDivide", "KeypadMultiply",
			"KeypadMinus", "KeypadPlus", "KeypadEnter", "KeypadPeriod", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21",
			"F22", "F23", "F24", "NonUsBackslash" };

		constexpr std::array<std::string_view, static_cast<std::size_t>(Platform::MouseButton::Count)> MouseNames = { "Unknown", "Left",
			"Middle", "Right", "X1", "X2" };

		constexpr std::array<std::string_view, static_cast<std::size_t>(Platform::GamepadButton::Count)> GamepadNames = { "Unknown",
			"South", "East", "West", "North", "Back", "Guide", "Start", "LeftStick", "RightStick", "LeftShoulder", "RightShoulder",
			"DpadUp", "DpadDown", "DpadLeft", "DpadRight" };

		constexpr std::array<std::string_view, static_cast<std::size_t>(Platform::GamepadAxis::Count)> AxisNames = { "Unknown", "LeftX",
			"LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };

		bool EqualsIgnoreCase(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
			{
				return false;
			}

			for (std::size_t i = 0; i < a.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				{
					return false;
				}
			}

			return true;
		}

		template <typename E, std::size_t N> bool Lookup(const std::array<std::string_view, N>& names, std::string_view text, E& value)
		{
			for (std::size_t i = 1; i < N; ++i)
			{
				if (EqualsIgnoreCase(names[i], text))
				{
					value = static_cast<E>(i);
					return true;
				}
			}

			return false;
		}

		template <typename E, std::size_t N> std::string_view Name(const std::array<std::string_view, N>& names, E value)
		{
			const auto index = static_cast<std::size_t>(value);
			return index < N ? names[index] : names[0];
		}

		bool StartsWith(std::string_view text, std::string_view prefix)
		{
			return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
		}

	} // namespace

	std::string_view ToString(Platform::ScanCode key)
	{
		return Name(ScanCodeNames, key);
	}

	std::string_view ToString(Platform::MouseButton button)
	{
		return Name(MouseNames, button);
	}

	std::string_view ToString(Platform::GamepadButton button)
	{
		return Name(GamepadNames, button);
	}

	std::string_view ToString(Platform::GamepadAxis axis)
	{
		return Name(AxisNames, axis);
	}

	std::string FormatBinding(const InputBinding& binding)
	{
		switch (binding.Type)
		{
		case InputBindingType::ScanCode:
			return std::string(ToString(binding.PhysicalKey));
		case InputBindingType::MouseButton:
			return "Mouse:" + std::string(ToString(binding.Mouse));
		case InputBindingType::MouseWheel:
			return binding.Scale >= 0.0f ? "Wheel:Up" : "Wheel:Down";
		case InputBindingType::GamepadButton:
			return "Pad:" + std::string(ToString(binding.Gamepad));
		case InputBindingType::GamepadAxis:
			return "Axis:" + std::string(ToString(binding.Axis)) + (binding.Scale >= 0.0f ? "+" : "-");
		case InputBindingType::Key:
			break;
		}

		return {};
	}

	bool ParseBinding(std::string_view text, InputBinding& binding)
	{
		InputBinding result;
		result.Action = binding.Action;

		if (StartsWith(text, "Mouse:"))
		{
			result.Type = InputBindingType::MouseButton;

			if (!Lookup(MouseNames, text.substr(6), result.Mouse))
			{
				return false;
			}
		}
		else if (StartsWith(text, "Wheel:"))
		{
			result.Type = InputBindingType::MouseWheel;
			const auto direction = text.substr(6);

			if (EqualsIgnoreCase(direction, "Up"))
			{
				result.Scale = 1.0f;
			}
			else if (EqualsIgnoreCase(direction, "Down"))
			{
				result.Scale = -1.0f;
			}
			else
			{
				return false;
			}
		}
		else if (StartsWith(text, "Pad:"))
		{
			result.Type = InputBindingType::GamepadButton;

			if (!Lookup(GamepadNames, text.substr(4), result.Gamepad))
			{
				return false;
			}
		}
		else if (StartsWith(text, "Axis:"))
		{
			result.Type = InputBindingType::GamepadAxis;
			auto name = text.substr(5);

			if (!name.empty() && (name.back() == '+' || name.back() == '-'))
			{
				result.Scale = name.back() == '-' ? -1.0f : 1.0f;
				name.remove_suffix(1);
			}

			if (!Lookup(AxisNames, name, result.Axis))
			{
				return false;
			}
		}
		else
		{
			result.Type = InputBindingType::ScanCode;

			if (!Lookup(ScanCodeNames, text, result.PhysicalKey))
			{
				return false;
			}
		}

		binding = result;
		return true;
	}

	bool SameInput(const InputBinding& a, const InputBinding& b)
	{
		if (a.Type != b.Type)
		{
			return false;
		}

		switch (a.Type)
		{
		case InputBindingType::Key:
			return a.Key == b.Key;
		case InputBindingType::ScanCode:
			return a.PhysicalKey == b.PhysicalKey;
		case InputBindingType::MouseButton:
			return a.Mouse == b.Mouse;
		case InputBindingType::GamepadButton:
			return a.Gamepad == b.Gamepad;
		case InputBindingType::GamepadAxis:
			return a.Axis == b.Axis && (a.Scale >= 0.0f) == (b.Scale >= 0.0f);
		case InputBindingType::MouseWheel:
			return (a.Scale >= 0.0f) == (b.Scale >= 0.0f);
		}

		return false;
	}

} // namespace Swim::Input
