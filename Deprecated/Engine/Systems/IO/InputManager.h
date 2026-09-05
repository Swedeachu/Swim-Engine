#pragma once

#include "Engine/Input/InputSystem.h"
#include "Engine/Machine.h"
#include <glm/glm.hpp>

namespace Engine
{

	class InputManager : public Machine
	{

	public:

		int Awake() override;

		int Init() override;

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) {}

		int Exit() override { return 0; }

		void ProcessInputEvent(const Swim::Platform::InputEvent& event) { inputSystem.ProcessInputEvent(event); }
		void ProcessWindowEvent(const Swim::Platform::WindowEvent& event) { inputSystem.ProcessWindowEvent(event); }

		bool IsKeyDown(Swim::Platform::KeyCode key) const { return inputSystem.IsKeyDown(key); }
		bool IsKeyTriggered(Swim::Platform::KeyCode key) const { return inputSystem.IsKeyTriggered(key); }
		bool IsKeyReleased(Swim::Platform::KeyCode key) const { return inputSystem.IsKeyReleased(key); }

		bool IsScanCodeDown(Swim::Platform::ScanCode scanCode) const { return inputSystem.IsScanCodeDown(scanCode); }
		bool IsScanCodeTriggered(Swim::Platform::ScanCode scanCode) const { return inputSystem.IsScanCodeTriggered(scanCode); }
		bool IsScanCodeReleased(Swim::Platform::ScanCode scanCode) const { return inputSystem.IsScanCodeReleased(scanCode); }

		bool IsMouseButtonDown(Swim::Platform::MouseButton button) const { return inputSystem.IsMouseButtonDown(button); }
		bool IsMouseButtonTriggered(Swim::Platform::MouseButton button) const { return inputSystem.IsMouseButtonTriggered(button); }
		bool IsMouseButtonReleased(Swim::Platform::MouseButton button) const { return inputSystem.IsMouseButtonReleased(button); }

		bool IsShiftDown() const { return inputSystem.IsShiftDown(); }
		bool IsControlDown() const { return inputSystem.IsControlDown(); }
		bool IsAltDown() const { return inputSystem.IsAltDown(); }

		float GetMouseScrollDelta() const { return inputSystem.GetMouseScrollDelta(); }
		glm::vec2 GetMousePosition() const;
		glm::vec2 GetMousePositionDelta() const;

		bool HasFocus() const { return inputSystem.HasFocus(); }
		Swim::Platform::Extent2D GetWindowSize() const { return inputSystem.GetWindowSize(); }

		const std::vector<std::string>& GetTextInput() const { return inputSystem.GetTextInput(); }
		const std::string& GetTextComposition() const { return inputSystem.GetTextComposition(); }
		int GetTextCompositionStart() const { return inputSystem.GetTextCompositionStart(); }
		int GetTextCompositionLength() const { return inputSystem.GetTextCompositionLength(); }

		bool IsGamepadConnected(Swim::Platform::InputDeviceId device) const { return inputSystem.IsGamepadConnected(device); }
		bool IsGamepadButtonDown(Swim::Platform::InputDeviceId device, Swim::Platform::GamepadButton button) const
		{
			return inputSystem.IsGamepadButtonDown(device, button);
		}
		float GetGamepadAxis(Swim::Platform::InputDeviceId device, Swim::Platform::GamepadAxis axis) const
		{
			return inputSystem.GetGamepadAxis(device, axis);
		}

		Swim::Input::InputSystem& GetInputSystem() { return inputSystem; }
		const Swim::Input::InputSystem& GetInputSystem() const { return inputSystem; }

	private:

		Swim::Input::InputSystem inputSystem;

	};

}
