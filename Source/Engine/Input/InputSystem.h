#pragma once

#include "InputMap.h"
#include "Engine/Platform/InputTypes.h"
#include "Engine/Platform/WindowEvents.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace Swim::Input
{

	// One key press (Text empty) or one committed text string, in event order.
	struct TextEditEvent
	{
		Platform::KeyCode Key = Platform::KeyCode::Unknown;
		std::string Text;
		bool Repeat = false;
	};

	class InputSystem
	{
	public:

		void Reset();
		// Publish once after event pumping, before fixed/update consumers read this frame.
		void AdvanceFrame();

		void ProcessInputEvent(const Platform::InputEvent& event);
		void ProcessWindowEvent(const Platform::WindowEvent& event);

		bool IsKeyDown(Platform::KeyCode key) const;
		bool IsKeyTriggered(Platform::KeyCode key) const;
		bool IsKeyReleased(Platform::KeyCode key) const;

		bool IsScanCodeDown(Platform::ScanCode scanCode) const;
		bool IsScanCodeTriggered(Platform::ScanCode scanCode) const;
		bool IsScanCodeReleased(Platform::ScanCode scanCode) const;

		bool IsMouseButtonDown(Platform::MouseButton button) const;
		bool IsMouseButtonTriggered(Platform::MouseButton button) const;
		bool IsMouseButtonReleased(Platform::MouseButton button) const;

		bool IsShiftDown() const;
		bool IsControlDown() const;
		bool IsAltDown() const;

		float GetMouseScrollDelta() const { return mouseWheelDelta; }
		Platform::Float2 GetMousePosition() const { return mousePosition; }
		Platform::Float2 GetMousePositionDelta() const { return mouseDelta; }

		bool HasFocus() const { return hasFocus; }
		Platform::Extent2D GetWindowSize() const { return windowSize; }

		const std::vector<std::string>& GetTextInput() const { return textInput; }
		// Every key press of the frame in event order, including operating-system key
		// repeats (IsKeyTriggered reports only the first press). For text editing.
		const std::vector<Platform::KeyCode>& GetKeyPresses() const { return keyPresses; }
		// Key presses and committed text interleaved in event order (typing "ab" and
		// pressing Left in one frame must not move the caret before the text arrives).
		const std::vector<TextEditEvent>& GetTextEditEvents() const { return textEditEvents; }
		// True when the platform reported an IME composition update this frame; the
		// composition (possibly empty, which ends it) is then GetTextComposition().
		bool HasTextCompositionUpdate() const { return textCompositionUpdated; }
		const std::string& GetTextComposition() const { return textComposition; }
		int GetTextCompositionStart() const { return textCompositionStart; }
		int GetTextCompositionLength() const { return textCompositionLength; }

		bool IsGamepadConnected(Platform::InputDeviceId device) const;
		bool IsGamepadButtonDown(Platform::InputDeviceId device, Platform::GamepadButton button) const;
		bool IsGamepadButtonTriggered(Platform::InputDeviceId device, Platform::GamepadButton button) const;
		bool IsGamepadButtonReleased(Platform::InputDeviceId device, Platform::GamepadButton button) const;
		float GetGamepadAxis(Platform::InputDeviceId device, Platform::GamepadAxis axis) const;

		float GetActionValue(InputAction action, const InputMap& map,
			Platform::InputDeviceId gamepadDevice = 0) const;
		bool IsActionDown(InputAction action, const InputMap& map,
			Platform::InputDeviceId gamepadDevice = 0) const;

	private:

		struct ButtonState
		{
			bool Current = false;
			bool Previous = false;
			bool Deferred = false;
		};

		struct GamepadState
		{
			std::array<ButtonState, static_cast<size_t>(Platform::GamepadButton::Count)> Buttons{};
			std::array<float, static_cast<size_t>(Platform::GamepadAxis::Count)> Axes{};
			std::array<float, static_cast<size_t>(Platform::GamepadAxis::Count)> DeferredAxes{};
		};

		template<typename T, size_t N>
		static void AdvanceButtonStates(std::array<T, N>& states)
		{
			for (auto& state : states)
			{
				state.Previous = state.Current;
				state.Current = state.Deferred;
			}
		}

		void ReleaseFocusSensitiveState();

		std::array<ButtonState, static_cast<size_t>(Platform::KeyCode::Count)> keyState{};
		std::array<ButtonState, static_cast<size_t>(Platform::ScanCode::Count)> scanCodeState{};
		std::array<ButtonState, static_cast<size_t>(Platform::MouseButton::Count)> mouseButtonState{};
		std::unordered_map<Platform::InputDeviceId, GamepadState> gamepads;

		bool hasFocus = true;
		Platform::Extent2D windowSize{};

		float mouseWheelDelta = 0.0f;
		float deferredMouseWheelDelta = 0.0f;
		Platform::Float2 mousePosition{};
		Platform::Float2 mouseDelta{};
		Platform::Float2 deferredMouseDelta{};

		std::vector<std::string> textInput;
		std::vector<std::string> deferredTextInput;
		std::vector<Platform::KeyCode> keyPresses;
		std::vector<Platform::KeyCode> deferredKeyPresses;
		std::vector<TextEditEvent> textEditEvents;
		std::vector<TextEditEvent> deferredTextEditEvents;
		bool textCompositionUpdated = false;
		bool deferredTextCompositionUpdated = false;
		std::string textComposition;
		std::string deferredTextComposition;
		int textCompositionStart = 0;
		int textCompositionLength = 0;
		int deferredTextCompositionStart = 0;
		int deferredTextCompositionLength = 0;

	};

}
