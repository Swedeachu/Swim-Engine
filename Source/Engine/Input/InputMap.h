#pragma once

#include "Engine/Platform/InputTypes.h"
#include <cstdint>
#include <vector>

namespace Swim::Input
{

	using InputAction = uint32_t;

	enum class InputBindingType : uint8_t
	{
		Key,
		ScanCode,
		MouseButton,
		GamepadButton,
		GamepadAxis
	};

	struct InputBinding
	{
		InputAction Action = 0;
		InputBindingType Type = InputBindingType::Key;
		Platform::KeyCode Key = Platform::KeyCode::Unknown;
		Platform::ScanCode PhysicalKey = Platform::ScanCode::Unknown;
		Platform::MouseButton Mouse = Platform::MouseButton::Unknown;
		Platform::GamepadButton Gamepad = Platform::GamepadButton::Unknown;
		Platform::GamepadAxis Axis = Platform::GamepadAxis::Unknown;
		float Scale = 1.0f;
	};

	class InputMap
	{
	public:

		void AddBinding(const InputBinding& binding) { bindings.push_back(binding); }
		void Clear() { bindings.clear(); }
		const std::vector<InputBinding>& GetBindings() const { return bindings; }

	private:

		std::vector<InputBinding> bindings;
	};

}
