#include "Engine/Systems/UiInput/UiInputBridge.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <stdexcept>

using namespace Swim;
using namespace Swim::UI;

namespace
{
	std::size_t Count(const std::vector<UiEvent>& events, UiEventKind kind, UiNodeId node)
	{
		return std::count_if(events.begin(), events.end(),
			[=](const auto& event)
			{
				return event.Kind == kind && event.Node == node;
			});
	}

	// An InputSystem fed with synthetic platform events, one frame at a time.
	struct Frame
	{
		Input::InputSystem& Input;

		void Mouse(float x, float y)
		{
			Platform::InputEvent event{};
			event.Type = Platform::InputEventType::MouseMove;
			event.Position = { x, y };
			Input.ProcessInputEvent(event);
		}

		void Button(bool down, float x, float y)
		{
			Platform::InputEvent event{};
			event.Type = down ? Platform::InputEventType::MouseButtonDown : Platform::InputEventType::MouseButtonUp;
			event.Mouse = Platform::MouseButton::Left;
			event.Position = { x, y };
			Input.ProcessInputEvent(event);
		}

		void Key(Platform::KeyCode key, bool down = true, bool repeat = false)
		{
			Platform::InputEvent event{};
			event.Type = down ? Platform::InputEventType::KeyDown : Platform::InputEventType::KeyUp;
			event.Key = key;
			event.Repeat = repeat;
			Input.ProcessInputEvent(event);
		}

		void Text(const char* text)
		{
			Platform::InputEvent event{};
			event.Type = Platform::InputEventType::TextInput;
			event.Text = text;
			Input.ProcessInputEvent(event);
		}

		void Composition(const char* text, int start)
		{
			Platform::InputEvent event{};
			event.Type = Platform::InputEventType::TextEditing;
			event.Text = text;
			event.EditStart = start;
			Input.ProcessInputEvent(event);
		}

		void Wheel(float y)
		{
			Platform::InputEvent event{};
			event.Type = Platform::InputEventType::MouseWheel;
			event.Delta = { 0.0f, y };
			Input.ProcessInputEvent(event);
		}
	};
} // namespace

SWIM_TEST("UiInput.Bridge", "RoutesPointerKeysTextCompositionAndFocusLoss")
{
	UiDocument ui;
	UiStyle buttonStyle;
	buttonStyle.Width = UiLength::Pixels(100);
	buttonStyle.Height = UiLength::Pixels(40);
	buttonStyle.HitTest = true;
	buttonStyle.Focusable = true;
	const auto button = ui.Create(ui.GetRoot());
	ui.SetStyle(button, buttonStyle);
	UiStyle fieldStyle = buttonStyle;
	fieldStyle.Width = UiLength::Pixels(300);
	const auto field = ui.Create(ui.GetRoot());
	ui.SetStyle(field, fieldStyle);
	ui.SetText(field, Swim::Testing::LoadTextFontChain(), "", 20);
	ui.SetEditable(field, true);
	ui.Layout({ 800, 600 }, 2.0f); // Framebuffer pixels are twice the window's coordinates.

	Input::InputSystem input;
	Frame frame{ input };
	UiInputBridgeDesc desc;
	desc.FramebufferScale = 2.0f;
	UiInputBridge bridge(desc);

	// Click the button (window coordinates 50, 20 -> logical 50, 20 at DPI 2).
	frame.Mouse(50, 20);
	frame.Button(true, 50, 20);
	input.AdvanceFrame();
	auto result = bridge.Apply(input, ui);
	SWIM_CHECK(result.PointerOverUi);
	SWIM_CHECK(ui.GetFocus() == button);
	SWIM_CHECK(!result.WantsTextInput);
	frame.Button(false, 50, 20);
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, button), 1u);
	frame.Key(Platform::KeyCode::Enter);
	input.AdvanceFrame();
	SWIM_CHECK_EQUAL(bridge.Apply(input, ui).KeysConsumed, 1u);
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, button), 1u); // Enter activates.
	frame.Key(Platform::KeyCode::Enter, false);

	// Tab moves to the field; typed text, repeated arrows and Control+A/Backspace edit it.
	frame.Key(Platform::KeyCode::Tab);
	input.AdvanceFrame();
	result = bridge.Apply(input, ui);
	SWIM_CHECK(ui.GetFocus() == field);
	SWIM_CHECK(result.WantsTextInput && result.KeyboardCaptured);
	frame.Key(Platform::KeyCode::Tab, false);
	frame.Text("hello");
	frame.Text(" world");
	frame.Key(Platform::KeyCode::Left);
	frame.Key(Platform::KeyCode::Left, true, true);
	frame.Key(Platform::KeyCode::Left, true, true);
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK_EQUAL(ui.GetText(field), std::string("hello world"));
	SWIM_CHECK_EQUAL(ui.GetSelection(field).Caret, 8u); // Three presses, two of them repeats.
	frame.Key(Platform::KeyCode::Left, false);
	frame.Key(Platform::KeyCode::LeftControl);
	frame.Key(Platform::KeyCode::A);
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	frame.Key(Platform::KeyCode::A, false);
	frame.Key(Platform::KeyCode::LeftControl, false);
	frame.Key(Platform::KeyCode::Backspace);
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK(ui.GetText(field).empty());
	frame.Key(Platform::KeyCode::Backspace, false);

	// IME: a preedit persists across frames without updates and ends with an empty one.
	frame.Composition("\xE3\x81\x8B\xE3\x81\x8B", 1); // Cursor after the first code point.
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK_EQUAL(ui.GetComposition(), std::string("\xE3\x81\x8B\xE3\x81\x8B"));
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK(!ui.GetComposition().empty());
	frame.Composition("", 0);
	frame.Text("\xE6\xBC\xA2");
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK(ui.GetComposition().empty());
	SWIM_CHECK_EQUAL(ui.GetText(field), std::string("\xE6\xBC\xA2"));

	// Application focus loss cancels capture and clears UI focus.
	frame.Button(true, 150, 60); // The field (logical y 40..80).
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	Platform::WindowEvent lost{};
	lost.Type = Platform::WindowEventType::FocusLost;
	input.ProcessWindowEvent(lost);
	input.AdvanceFrame();
	result = bridge.Apply(input, ui);
	SWIM_CHECK(!ui.GetFocus());
	SWIM_CHECK(!result.WantsTextInput);
	SWIM_CHECK(Count(ui.DrainEvents(), UiEventKind::Cancel, field) >= 1u);
}

SWIM_TEST("UiInput.Bridge", "WheelScrollsAndShortcutModifierIsConfigurable")
{
	UiDocument ui;
	UiStyle clipStyle;
	clipStyle.Width = UiLength::Pixels(100);
	clipStyle.Height = UiLength::Pixels(100);
	clipStyle.Clip = true;
	const auto list = ui.Create(ui.GetRoot());
	ui.SetStyle(list, clipStyle);
	UiStyle tall;
	tall.Width = UiLength::Pixels(100);
	tall.Height = UiLength::Pixels(500);
	ui.SetStyle(ui.Create(list), tall);
	ui.Layout({ 400, 400 });
	Input::InputSystem input;
	Frame frame{ input };
	UiInputBridge bridge;
	frame.Mouse(50, 50);
	frame.Wheel(-2.0f); // Two notches down.
	input.AdvanceFrame();
	const auto result = bridge.Apply(input, ui);
	SWIM_CHECK(!result.PointerOverUi); // Scrollable, but not hit-testable.
	ui.Layout({ 400, 400 });
	SWIM_CHECK_NEAR(ui.GetScroll(list).Y, 96.0f, 1e-4f);
	SWIM_CHECK_THROWS(UiInputBridge({ 0.0f }), std::invalid_argument);
	UiInputBridgeDesc mac;
	mac.Shortcut = UiShortcutModifier::Super;
	UiInputBridge macBridge(mac);
	(void)macBridge;
}
