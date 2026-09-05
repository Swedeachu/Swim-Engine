#include "Engine/Input/InputSystem.h"
#include "Tests/Framework/Test.h"

SWIM_TEST("Input.Frame", "SimulationAndPresentationReadTheSamePublishedSnapshot")
{
	using namespace Swim;
	Input::InputSystem input;
	Platform::InputEvent key{};
	key.Type = Platform::InputEventType::KeyDown;
	key.Key = Platform::KeyCode::W;
	input.ProcessInputEvent(key);
	Platform::InputEvent mouse{};
	mouse.Type = Platform::InputEventType::MouseMove;
	mouse.Delta = { 3.0f, -2.0f };
	input.ProcessInputEvent(mouse);
	SWIM_CHECK(!input.IsKeyDown(Platform::KeyCode::W));
	input.AdvanceFrame();
	// Two fixed steps and presentation consume one frame snapshot without advancing it.
	for (int consumer = 0; consumer < 3; ++consumer)
	{
		SWIM_CHECK(input.IsKeyTriggered(Platform::KeyCode::W));
		SWIM_CHECK_EQUAL(input.GetMousePositionDelta().X, 3.0f);
		SWIM_CHECK_EQUAL(input.GetMousePositionDelta().Y, -2.0f);
	}
	input.AdvanceFrame();
	SWIM_CHECK(input.IsKeyDown(Platform::KeyCode::W));
	SWIM_CHECK(!input.IsKeyTriggered(Platform::KeyCode::W));
	SWIM_CHECK_EQUAL(input.GetMousePositionDelta().X, 0.0f);
}

SWIM_TEST("Input.Frame", "FocusLossClearsDeferredInputBeforeTheNextSnapshot")
{
	using namespace Swim;
	Input::InputSystem input;
	Platform::InputEvent key{};
	key.Type = Platform::InputEventType::KeyDown;
	key.Key = Platform::KeyCode::W;
	input.ProcessInputEvent(key);
	Platform::InputEvent mouse{};
	mouse.Type = Platform::InputEventType::MouseMove;
	mouse.Delta = { 4.0f, 5.0f };
	input.ProcessInputEvent(mouse);
	Platform::WindowEvent focus{};
	focus.Type = Platform::WindowEventType::FocusLost;
	input.ProcessWindowEvent(focus);
	input.AdvanceFrame();
	SWIM_CHECK(!input.HasFocus());
	SWIM_CHECK(!input.IsKeyDown(Platform::KeyCode::W));
	SWIM_CHECK_EQUAL(input.GetMousePositionDelta().X, 0.0f);
	SWIM_CHECK_EQUAL(input.GetMousePositionDelta().Y, 0.0f);
	input.Reset();
	SWIM_CHECK(input.HasFocus());
	SWIM_CHECK(!input.IsKeyTriggered(Platform::KeyCode::W));
}
