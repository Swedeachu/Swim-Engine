#include "Engine/Input/InputSystem.h"
#include "Tests/Framework/Test.h"

SWIM_TEST("Input.InputSystem", "WindowResizeUpdatesLogicalSize")
{
	Swim::Input::InputSystem input;
	input.Reset();

	Swim::Platform::WindowEvent sizeEvent{};
	sizeEvent.Type = Swim::Platform::WindowEventType::Resized;
	sizeEvent.LogicalSize = { 1280, 720 };
	input.ProcessWindowEvent(sizeEvent);

	SWIM_CHECK_EQUAL(input.GetWindowSize().Width, 1280);
	SWIM_CHECK_EQUAL(input.GetWindowSize().Height, 720);
}

SWIM_TEST("Input.InputSystem", "KeyAndScanCodeEdgeStates")
{
	using namespace Swim;

	Input::InputSystem input;
	input.Reset();

	Platform::InputEvent keyDown{};
	keyDown.Type = Platform::InputEventType::KeyDown;
	keyDown.Key = Platform::KeyCode::W;
	keyDown.PhysicalKey = Platform::ScanCode::W;
	input.ProcessInputEvent(keyDown);
	input.AdvanceFrame();

	SWIM_CHECK(input.IsKeyDown(Platform::KeyCode::W));
	SWIM_CHECK(input.IsKeyTriggered(Platform::KeyCode::W));
	SWIM_CHECK(input.IsScanCodeDown(Platform::ScanCode::W));
	SWIM_CHECK(input.IsScanCodeTriggered(Platform::ScanCode::W));

	// A held key stays down but is only "triggered" on the frame it arrived.
	input.AdvanceFrame();
	SWIM_CHECK(input.IsKeyDown(Platform::KeyCode::W));
	SWIM_CHECK(!input.IsKeyTriggered(Platform::KeyCode::W));

	Platform::InputEvent keyUp = keyDown;
	keyUp.Type = Platform::InputEventType::KeyUp;
	input.ProcessInputEvent(keyUp);
	input.AdvanceFrame();
	SWIM_CHECK(!input.IsKeyDown(Platform::KeyCode::W));
	SWIM_CHECK(input.IsKeyReleased(Platform::KeyCode::W));
}

SWIM_TEST("Input.InputSystem", "MousePositionAndDelta")
{
	using namespace Swim;

	Input::InputSystem input;
	input.Reset();

	Platform::InputEvent mouseMove{};
	mouseMove.Type = Platform::InputEventType::MouseMove;
	mouseMove.Position = { 400.0f, 300.0f };
	mouseMove.Delta = { 2.0f, -4.0f };
	input.ProcessInputEvent(mouseMove);
	input.AdvanceFrame();

	SWIM_CHECK_EQUAL(input.GetMousePosition().X, 400.0f);
	SWIM_CHECK_EQUAL(input.GetMousePositionDelta().X, 2.0f);
	SWIM_CHECK_EQUAL(input.GetMousePositionDelta().Y, -4.0f);
}

SWIM_TEST("Input.InputSystem", "GamepadAxisFeedsActionMaps")
{
	using namespace Swim;

	Input::InputSystem input;
	input.Reset();

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

	SWIM_CHECK(input.IsGamepadConnected(7));
	SWIM_CHECK_NEAR(input.GetGamepadAxis(7, Platform::GamepadAxis::LeftX), 0.75f, 0.0001f);

	Input::InputMap movement;
	Input::InputBinding movementAxis{};
	movementAxis.Action = 1;
	movementAxis.Type = Input::InputBindingType::GamepadAxis;
	movementAxis.Axis = Platform::GamepadAxis::LeftX;
	movement.AddBinding(movementAxis);
	SWIM_CHECK_NEAR(input.GetActionValue(1, movement, 7), 0.75f, 0.0001f);
}

SWIM_TEST("Input.InputSystem", "FocusLossClearsLiveState")
{
	using namespace Swim;

	Input::InputSystem input;
	input.Reset();

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

	Platform::WindowEvent focusLost{};
	focusLost.Type = Platform::WindowEventType::FocusLost;
	input.ProcessWindowEvent(focusLost);

	SWIM_CHECK(!input.HasFocus());
	SWIM_CHECK_EQUAL(input.GetGamepadAxis(7, Platform::GamepadAxis::LeftX), 0.0f);
}
