#include "Engine/Input/InputSystem.h"
#include <cassert>
#include <cmath>

int main()
{
	using namespace Swim;

	Input::InputSystem input;
	input.Reset();

	Platform::WindowEvent sizeEvent{};
	sizeEvent.Type = Platform::WindowEventType::Resized;
	sizeEvent.LogicalSize = { 1280, 720 };
	input.ProcessWindowEvent(sizeEvent);
	assert(input.GetWindowSize().Width == 1280);
	assert(input.GetWindowSize().Height == 720);

	Platform::InputEvent keyDown{};
	keyDown.Type = Platform::InputEventType::KeyDown;
	keyDown.Key = Platform::KeyCode::W;
	keyDown.PhysicalKey = Platform::ScanCode::W;
	input.ProcessInputEvent(keyDown);
	input.AdvanceFrame();
	assert(input.IsKeyDown(Platform::KeyCode::W));
	assert(input.IsKeyTriggered(Platform::KeyCode::W));
	assert(input.IsScanCodeDown(Platform::ScanCode::W));
	assert(input.IsScanCodeTriggered(Platform::ScanCode::W));

	input.AdvanceFrame();
	assert(input.IsKeyDown(Platform::KeyCode::W));
	assert(!input.IsKeyTriggered(Platform::KeyCode::W));

	Platform::InputEvent keyUp = keyDown;
	keyUp.Type = Platform::InputEventType::KeyUp;
	input.ProcessInputEvent(keyUp);
	input.AdvanceFrame();
	assert(!input.IsKeyDown(Platform::KeyCode::W));
	assert(input.IsKeyReleased(Platform::KeyCode::W));

	Platform::InputEvent mouseMove{};
	mouseMove.Type = Platform::InputEventType::MouseMove;
	mouseMove.Position = { 400.0f, 300.0f };
	mouseMove.Delta = { 2.0f, -4.0f };
	input.ProcessInputEvent(mouseMove);
	input.AdvanceFrame();
	assert(input.GetMousePosition().X == 400.0f);
	assert(input.GetMousePositionDelta().X == 2.0f);
	assert(input.GetMousePositionDelta().Y == -4.0f);

	Platform::InputEvent gamepadAdded{};
	gamepadAdded.Type = Platform::InputEventType::GamepadAdded;
	gamepadAdded.Device = 7;
	input.ProcessInputEvent(gamepadAdded);

	Platform::InputEvent axis{};
	axis.Type = Platform::InputEventType::GamepadAxisMotion;
	axis.Device = 7;
	axis.Axis = Platform::GamepadAxis::LeftX;
	axis.AxisValue = 0.75f;
	input.ProcessInputEvent(axis);
	input.AdvanceFrame();
	assert(input.IsGamepadConnected(7));
	assert(std::abs(input.GetGamepadAxis(7, Platform::GamepadAxis::LeftX) - 0.75f) < 0.0001f);

	Input::InputMap movement;
	Input::InputBinding movementAxis{};
	movementAxis.Action = 1;
	movementAxis.Type = Input::InputBindingType::GamepadAxis;
	movementAxis.Axis = Platform::GamepadAxis::LeftX;
	movement.AddBinding(movementAxis);
	assert(std::abs(input.GetActionValue(1, movement, 7) - 0.75f) < 0.0001f);

	Platform::WindowEvent focusLost{};
	focusLost.Type = Platform::WindowEventType::FocusLost;
	input.ProcessWindowEvent(focusLost);
	assert(!input.HasFocus());
	assert(input.GetGamepadAxis(7, Platform::GamepadAxis::LeftX) == 0.0f);

	return 0;
}
