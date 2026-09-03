#include "InputSystem.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Swim::Input
{

	namespace
	{

		template<typename T>
		size_t ToIndex(T value)
		{
			return static_cast<size_t>(value);
		}

	}

	void InputSystem::Reset()
	{
		keyState = {};
		scanCodeState = {};
		mouseButtonState = {};
		gamepads.clear();
		hasFocus = true;
		windowSize = {};
		mouseWheelDelta = 0.0f;
		deferredMouseWheelDelta = 0.0f;
		mousePosition = {};
		mouseDelta = {};
		deferredMouseDelta = {};
		textInput.clear();
		deferredTextInput.clear();
		textComposition.clear();
		deferredTextComposition.clear();
		textCompositionStart = 0;
		textCompositionLength = 0;
		deferredTextCompositionStart = 0;
		deferredTextCompositionLength = 0;
	}

	void InputSystem::AdvanceFrame()
	{
		AdvanceButtonStates(keyState);
		AdvanceButtonStates(scanCodeState);
		AdvanceButtonStates(mouseButtonState);

		for (auto& [device, gamepad] : gamepads)
		{
			(void)device;
			AdvanceButtonStates(gamepad.Buttons);
			gamepad.Axes = gamepad.DeferredAxes;
		}

		if (!hasFocus)
		{
			ReleaseFocusSensitiveState();
		}

		mouseDelta = deferredMouseDelta;
		deferredMouseDelta = {};
		mouseWheelDelta = deferredMouseWheelDelta;
		deferredMouseWheelDelta = 0.0f;

		textInput = std::move(deferredTextInput);
		deferredTextInput.clear();
		textComposition = std::move(deferredTextComposition);
		deferredTextComposition.clear();
		textCompositionStart = deferredTextCompositionStart;
		textCompositionLength = deferredTextCompositionLength;
		deferredTextCompositionStart = 0;
		deferredTextCompositionLength = 0;
	}

	void InputSystem::ProcessInputEvent(const Platform::InputEvent& event)
	{
		using namespace Platform;

		switch (event.Type)
		{
			case InputEventType::KeyDown:
			case InputEventType::KeyUp:
			{
				const bool down = event.Type == InputEventType::KeyDown;
				const size_t keyIndex = ToIndex(event.Key);
				if (keyIndex > 0 && keyIndex < keyState.size())
				{
					keyState[keyIndex].Deferred = down;
				}

				const size_t scanCodeIndex = ToIndex(event.PhysicalKey);
				if (scanCodeIndex > 0 && scanCodeIndex < scanCodeState.size())
				{
					scanCodeState[scanCodeIndex].Deferred = down;
				}
				break;
			}

			case InputEventType::MouseButtonDown:
			case InputEventType::MouseButtonUp:
			{
				const size_t index = ToIndex(event.Mouse);
				if (index > 0 && index < mouseButtonState.size())
				{
					mouseButtonState[index].Deferred = event.Type == InputEventType::MouseButtonDown;
				}
				mousePosition = event.Position;
				break;
			}

			case InputEventType::MouseMove:
				mousePosition = event.Position;
				deferredMouseDelta.X += event.Delta.X;
				deferredMouseDelta.Y += event.Delta.Y;
				break;

			case InputEventType::MouseWheel:
				deferredMouseWheelDelta += event.Delta.Y;
				break;

			case InputEventType::TextInput:
				if (!event.Text.empty())
				{
					deferredTextInput.push_back(event.Text);
				}
				break;

			case InputEventType::TextEditing:
				deferredTextComposition = event.Text;
				deferredTextCompositionStart = event.EditStart;
				deferredTextCompositionLength = event.EditLength;
				break;

			case InputEventType::GamepadAdded:
				gamepads.try_emplace(event.Device);
				break;

			case InputEventType::GamepadRemoved:
				gamepads.erase(event.Device);
				break;

			case InputEventType::GamepadButtonDown:
			case InputEventType::GamepadButtonUp:
			{
				auto& gamepad = gamepads[event.Device];
				const size_t index = ToIndex(event.Gamepad);
				if (index > 0 && index < gamepad.Buttons.size())
				{
					gamepad.Buttons[index].Deferred = event.Type == InputEventType::GamepadButtonDown;
				}
				break;
			}

			case InputEventType::GamepadAxisMotion:
			{
				auto& gamepad = gamepads[event.Device];
				const size_t index = ToIndex(event.Axis);
				if (index > 0 && index < gamepad.DeferredAxes.size())
				{
					gamepad.DeferredAxes[index] = std::clamp(event.AxisValue, -1.0f, 1.0f);
				}
				break;
			}
		}
	}

	void InputSystem::ProcessWindowEvent(const Platform::WindowEvent& event)
	{
		using namespace Platform;

		if (event.LogicalSize.Width > 0 && event.LogicalSize.Height > 0)
		{
			windowSize = event.LogicalSize;
		}

		if (event.Type == WindowEventType::FocusGained)
		{
			hasFocus = true;
		}
		else if (event.Type == WindowEventType::FocusLost)
		{
			hasFocus = false;
			ReleaseFocusSensitiveState();
		}
	}

	bool InputSystem::IsKeyDown(Platform::KeyCode key) const
	{
		const size_t index = ToIndex(key);
		return index > 0 && index < keyState.size() && keyState[index].Current;
	}

	bool InputSystem::IsKeyTriggered(Platform::KeyCode key) const
	{
		const size_t index = ToIndex(key);
		return index > 0 && index < keyState.size() && keyState[index].Current && !keyState[index].Previous;
	}

	bool InputSystem::IsKeyReleased(Platform::KeyCode key) const
	{
		const size_t index = ToIndex(key);
		return index > 0 && index < keyState.size() && !keyState[index].Current && keyState[index].Previous;
	}

	bool InputSystem::IsScanCodeDown(Platform::ScanCode scanCode) const
	{
		const size_t index = ToIndex(scanCode);
		return index > 0 && index < scanCodeState.size() && scanCodeState[index].Current;
	}

	bool InputSystem::IsScanCodeTriggered(Platform::ScanCode scanCode) const
	{
		const size_t index = ToIndex(scanCode);
		return index > 0 && index < scanCodeState.size() && scanCodeState[index].Current && !scanCodeState[index].Previous;
	}

	bool InputSystem::IsScanCodeReleased(Platform::ScanCode scanCode) const
	{
		const size_t index = ToIndex(scanCode);
		return index > 0 && index < scanCodeState.size() && !scanCodeState[index].Current && scanCodeState[index].Previous;
	}

	bool InputSystem::IsMouseButtonDown(Platform::MouseButton button) const
	{
		const size_t index = ToIndex(button);
		return index > 0 && index < mouseButtonState.size() && mouseButtonState[index].Current;
	}

	bool InputSystem::IsMouseButtonTriggered(Platform::MouseButton button) const
	{
		const size_t index = ToIndex(button);
		return index > 0 && index < mouseButtonState.size() && mouseButtonState[index].Current && !mouseButtonState[index].Previous;
	}

	bool InputSystem::IsMouseButtonReleased(Platform::MouseButton button) const
	{
		const size_t index = ToIndex(button);
		return index > 0 && index < mouseButtonState.size() && !mouseButtonState[index].Current && mouseButtonState[index].Previous;
	}

	bool InputSystem::IsShiftDown() const
	{
		return IsKeyDown(Platform::KeyCode::LeftShift) || IsKeyDown(Platform::KeyCode::RightShift);
	}

	bool InputSystem::IsControlDown() const
	{
		return IsKeyDown(Platform::KeyCode::LeftControl) || IsKeyDown(Platform::KeyCode::RightControl);
	}

	bool InputSystem::IsAltDown() const
	{
		return IsKeyDown(Platform::KeyCode::LeftAlt) || IsKeyDown(Platform::KeyCode::RightAlt);
	}

	bool InputSystem::IsGamepadConnected(Platform::InputDeviceId device) const
	{
		return gamepads.contains(device);
	}

	bool InputSystem::IsGamepadButtonDown(Platform::InputDeviceId device, Platform::GamepadButton button) const
	{
		auto it = gamepads.find(device);
		if (it == gamepads.end())
		{
			return false;
		}

		const size_t index = ToIndex(button);
		return index > 0 && index < it->second.Buttons.size() && it->second.Buttons[index].Current;
	}

	bool InputSystem::IsGamepadButtonTriggered(Platform::InputDeviceId device, Platform::GamepadButton button) const
	{
		auto it = gamepads.find(device);
		if (it == gamepads.end())
		{
			return false;
		}

		const size_t index = ToIndex(button);
		return index > 0 && index < it->second.Buttons.size() && it->second.Buttons[index].Current &&
			!it->second.Buttons[index].Previous;
	}

	bool InputSystem::IsGamepadButtonReleased(Platform::InputDeviceId device, Platform::GamepadButton button) const
	{
		auto it = gamepads.find(device);
		if (it == gamepads.end())
		{
			return false;
		}

		const size_t index = ToIndex(button);
		return index > 0 && index < it->second.Buttons.size() && !it->second.Buttons[index].Current &&
			it->second.Buttons[index].Previous;
	}

	float InputSystem::GetGamepadAxis(Platform::InputDeviceId device, Platform::GamepadAxis axis) const
	{
		auto it = gamepads.find(device);
		if (it == gamepads.end())
		{
			return 0.0f;
		}

		const size_t index = ToIndex(axis);
		return index > 0 && index < it->second.Axes.size() ? it->second.Axes[index] : 0.0f;
	}

	float InputSystem::GetActionValue(InputAction action, const InputMap& map,
		Platform::InputDeviceId gamepadDevice) const
	{
		float value = 0.0f;

		for (const InputBinding& binding : map.GetBindings())
		{
			if (binding.Action != action)
			{
				continue;
			}

			switch (binding.Type)
			{
				case InputBindingType::Key:
					if (IsKeyDown(binding.Key))
					{
						value += binding.Scale;
					}
					break;

				case InputBindingType::ScanCode:
					if (IsScanCodeDown(binding.PhysicalKey))
					{
						value += binding.Scale;
					}
					break;

				case InputBindingType::MouseButton:
					if (IsMouseButtonDown(binding.Mouse))
					{
						value += binding.Scale;
					}
					break;

				case InputBindingType::GamepadButton:
					if (IsGamepadButtonDown(gamepadDevice, binding.Gamepad))
					{
						value += binding.Scale;
					}
					break;

				case InputBindingType::GamepadAxis:
					value += GetGamepadAxis(gamepadDevice, binding.Axis) * binding.Scale;
					break;
			}
		}

		return std::clamp(value, -1.0f, 1.0f);
	}

	bool InputSystem::IsActionDown(InputAction action, const InputMap& map,
		Platform::InputDeviceId gamepadDevice) const
	{
		return std::abs(GetActionValue(action, map, gamepadDevice)) > 0.0001f;
	}

	void InputSystem::ReleaseFocusSensitiveState()
	{
		for (auto& key : keyState)
		{
			key.Current = false;
			key.Deferred = false;
		}

		for (auto& scanCode : scanCodeState)
		{
			scanCode.Current = false;
			scanCode.Deferred = false;
		}

		for (auto& button : mouseButtonState)
		{
			button.Current = false;
			button.Deferred = false;
		}

		for (auto& [device, gamepad] : gamepads)
		{
			(void)device;
			for (auto& button : gamepad.Buttons)
			{
				button.Current = false;
				button.Deferred = false;
			}
			gamepad.Axes.fill(0.0f);
			gamepad.DeferredAxes.fill(0.0f);
		}

		mouseWheelDelta = 0.0f;
		deferredMouseWheelDelta = 0.0f;
		mouseDelta = {};
		deferredMouseDelta = {};
		textInput.clear();
		deferredTextInput.clear();
		textComposition.clear();
		deferredTextComposition.clear();
		textCompositionStart = 0;
		textCompositionLength = 0;
		deferredTextCompositionStart = 0;
		deferredTextCompositionLength = 0;
	}

}
