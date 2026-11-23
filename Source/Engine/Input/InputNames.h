#pragma once

#include "Engine/Input/InputMap.h"

#include <string>
#include <string_view>

namespace Swim::Input
{

	// Stable names for physical inputs, used in config files and settings screens. Keys are
	// physical (scan code) names in US-layout terms ("W", "Space", "LeftBracket"); a
	// settings UI shows the user's layout label on top of these.
	std::string_view ToString(Platform::ScanCode key);

	std::string_view ToString(Platform::MouseButton button);

	std::string_view ToString(Platform::GamepadButton button);

	std::string_view ToString(Platform::GamepadAxis axis);

	// Binding text (case-insensitive on parse):
	//   <ScanCode>               W, Space, LeftShift, Keypad5
	//   Mouse:<Left|Middle|Right|X1|X2>
	//   Wheel:<Up|Down>
	//   Pad:<South|East|...>     gamepad button
	//   Axis:<LeftX|...>[+|-]    gamepad axis, + or - selects one direction
	// Bindings by layout key code (InputBindingType::Key) have no text form.
	std::string FormatBinding(const InputBinding& binding);

	bool ParseBinding(std::string_view text, InputBinding& binding);

	bool SameInput(const InputBinding& a, const InputBinding& b);

} // namespace Swim::Input
