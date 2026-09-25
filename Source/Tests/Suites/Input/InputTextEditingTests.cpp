#include "Engine/Input/InputSystem.h"
#include "Tests/Framework/Test.h"

SWIM_TEST("Input.TextEditing", "KeyPressesIncludeRepeatsAndCompositionUpdatesAreFlagged")
{
	using namespace Swim;
	Input::InputSystem input;
	Platform::InputEvent press{};
	press.Type = Platform::InputEventType::KeyDown;
	press.Key = Platform::KeyCode::Backspace;
	input.ProcessInputEvent(press);
	press.Repeat = true;
	input.ProcessInputEvent(press);
	input.ProcessInputEvent(press);
	Platform::InputEvent editing{};
	editing.Type = Platform::InputEventType::TextEditing;
	editing.Text = "ka";
	editing.EditStart = 1;
	input.ProcessInputEvent(editing);
	input.AdvanceFrame();
	SWIM_REQUIRE_EQUAL(input.GetKeyPresses().size(), std::size_t(3));
	SWIM_REQUIRE_EQUAL(input.GetTextEditEvents().size(), std::size_t(3));
	SWIM_CHECK(input.GetTextEditEvents()[1].Repeat && !input.GetTextEditEvents()[0].Repeat);
	SWIM_CHECK(input.GetKeyPresses()[2] == Platform::KeyCode::Backspace);
	SWIM_CHECK(input.IsKeyTriggered(Platform::KeyCode::Backspace)); // Still one trigger.
	SWIM_CHECK(input.HasTextCompositionUpdate());
	SWIM_CHECK_EQUAL(input.GetTextComposition(), std::string("ka"));
	SWIM_CHECK_EQUAL(input.GetTextCompositionStart(), 1);

	Platform::InputEvent typed{};
	typed.Type = Platform::InputEventType::TextInput;
	typed.Text = "a";
	input.ProcessInputEvent(typed);
	press.Repeat = false;
	press.Key = Platform::KeyCode::Left;
	input.ProcessInputEvent(press);
	typed.Text = "b";
	input.ProcessInputEvent(typed);
	input.AdvanceFrame();
	SWIM_CHECK_EQUAL(input.GetKeyPresses().size(), std::size_t(1));
	SWIM_CHECK(!input.HasTextCompositionUpdate()); // No news: the composition is unchanged.
	// Text and keys keep their interleaving.
	const auto& events = input.GetTextEditEvents();
	SWIM_REQUIRE_EQUAL(events.size(), std::size_t(3));
	SWIM_CHECK(events[0].Text == "a" && events[1].Key == Platform::KeyCode::Left && events[2].Text == "b");
	input.AdvanceFrame();
	SWIM_CHECK(input.GetTextEditEvents().empty());

	editing.Text.clear(); // The IME ended the composition.
	input.ProcessInputEvent(editing);
	input.ProcessInputEvent(press);
	Platform::WindowEvent focus{};
	focus.Type = Platform::WindowEventType::FocusLost;
	input.ProcessWindowEvent(focus);
	input.AdvanceFrame();
	SWIM_CHECK(input.GetKeyPresses().empty()); // Focus loss drops pending presses.
	SWIM_CHECK(input.GetTextEditEvents().empty());
	SWIM_CHECK(!input.HasTextCompositionUpdate());
}
