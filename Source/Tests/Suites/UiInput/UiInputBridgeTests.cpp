#include "Engine/Systems/UI/UiWidgets.h"
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

		void RightButton(bool down, float x, float y)
		{
			Platform::InputEvent event{};
			event.Type = down ? Platform::InputEventType::MouseButtonDown : Platform::InputEventType::MouseButtonUp;
			event.Mouse = Platform::MouseButton::Right;
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

		void Pad(Platform::InputDeviceId device)
		{
			Platform::InputEvent event{};
			event.Type = Platform::InputEventType::GamepadAdded;
			event.Device = device;
			Input.ProcessInputEvent(event);
		}

		void PadButton(Platform::InputDeviceId device, Platform::GamepadButton button, bool down)
		{
			Platform::InputEvent event{};
			event.Type = down ? Platform::InputEventType::GamepadButtonDown : Platform::InputEventType::GamepadButtonUp;
			event.Device = device;
			event.Gamepad = button;
			Input.ProcessInputEvent(event);
		}

		void PadAxis(Platform::InputDeviceId device, Platform::GamepadAxis axis, float value)
		{
			Platform::InputEvent event{};
			event.Type = Platform::InputEventType::GamepadAxisMotion;
			event.Device = device;
			event.Axis = axis;
			event.AxisValue = value;
			Input.ProcessInputEvent(event);
		}
	};

	std::shared_ptr<UiTheme> FontTheme()
	{
		auto theme = std::make_shared<UiTheme>();
		theme->Fonts = Swim::Testing::LoadTextFontChain();
		return theme;
	}
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
	UiInputBridgeDesc invalid;
	invalid.FramebufferScale = 0.0f;
	SWIM_CHECK_THROWS(UiInputBridge{ invalid }, std::invalid_argument);
	invalid = {};
	invalid.RepeatIntervalSeconds = 0.0f;
	SWIM_CHECK_THROWS(UiInputBridge{ invalid }, std::invalid_argument);
	UiInputBridgeDesc mac;
	mac.Shortcut = UiShortcutModifier::Super;
	UiInputBridge macBridge(mac);
	(void)macBridge;
}

SWIM_TEST("UiInput.Bridge", "GamepadNavigatesAdjustsSlidersActivatesAndRepeatsHeldDirections")
{
	using B = Platform::GamepadButton;
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto checkbox = CreateCheckbox(ui, ui.GetRoot(), "Invert Y");
	const auto slider = CreateSlider(ui, ui.GetRoot(), { .Min = 0.0f, .Max = 10.0f, .Step = 1.0f });
	const auto button = CreateButton(ui, ui.GetRoot(), "Back");
	ui.Layout({ 400, 300 });
	Input::InputSystem input;
	Frame frame{ input };
	constexpr Platform::InputDeviceId pad = 7;
	frame.Pad(pad);
	UiInputBridgeDesc desc;
	desc.Gamepad = pad;
	UiInputBridge bridge(desc);
	const auto press = [&](B buttonId, float seconds = 0.016f)
	{
		frame.PadButton(pad, buttonId, true);
		input.AdvanceFrame();
		const auto result = bridge.Apply(input, ui, seconds);
		frame.PadButton(pad, buttonId, false);
		input.AdvanceFrame();
		bridge.Apply(input, ui, seconds);
		return result;
	};

	// The first direction focuses the first control; South activates.
	auto result = press(B::DpadDown);
	SWIM_CHECK(ui.GetFocus() == checkbox);
	SWIM_CHECK(result.GamepadCaptured);
	press(B::South);
	SWIM_CHECK(ui.GetChecked(checkbox) == UiCheckState::Checked);
	press(B::DpadDown);
	SWIM_CHECK(ui.GetFocus() == slider);

	// Right adjusts the focused slider; holding repeats after the delay.
	frame.PadButton(pad, B::DpadRight, true);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.016f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 1.0f, 1e-6f);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.3f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 1.0f, 1e-6f); // Within the repeat delay.
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.15f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 2.0f, 1e-6f);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.1f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 3.0f, 1e-6f);
	frame.PadButton(pad, B::DpadRight, false);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.016f);

	// The left stick navigates past its threshold (one step per push).
	frame.PadAxis(pad, Platform::GamepadAxis::LeftY, 0.9f);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.016f);
	SWIM_CHECK(ui.GetFocus() == button);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.016f);
	SWIM_CHECK(ui.GetFocus() == button);
	frame.PadAxis(pad, Platform::GamepadAxis::LeftY, 0.1f);
	input.AdvanceFrame();
	bridge.Apply(input, ui, 0.016f);
	press(B::South);
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, button), 1u);

	// East is Escape; the shoulders are Shift+Tab / Tab.
	result = press(B::East);
	SWIM_CHECK(!ui.GetFocus());
	press(B::RightShoulder);
	SWIM_CHECK(ui.GetFocus() == checkbox);
	press(B::LeftShoulder);
	SWIM_CHECK(ui.GetFocus() == button);

	// Without a configured gamepad the pad is left to the game.
	UiInputBridge keyboardOnly;
	ui.Focus({});
	frame.PadButton(pad, B::DpadDown, true);
	input.AdvanceFrame();
	SWIM_CHECK(!keyboardOnly.Apply(input, ui, 0.016f).GamepadCaptured);
	SWIM_CHECK(!ui.GetFocus());
	SWIM_CHECK_THROWS(bridge.Apply(input, ui, -1.0f), std::invalid_argument);
}

SWIM_TEST("UiInput.Bridge", "RouterFramesCastTheMouseIntoWorldCanvasesThroughTheCamera")
{
	UiDocument world;
	world.SetTheme(FontTheme());
	const auto button = CreateButton(world, world.GetRoot(), "Open");
	world.Layout({ 400, 200 });
	UiCameraView camera;
	camera.View = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -5, 0, 0, 0, 1 };
	camera.Projection = { 0.75f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.1f, 0, 0, -1, 0 };
	camera.ViewportWidth = 800.0f;
	camera.ViewportHeight = 600.0f;
	UiCanvasRouter router;
	UiCanvasDesc canvasDesc;
	canvasDesc.Document = &world;
	canvasDesc.Mode = UiCanvasMode::WorldPanel;
	const auto canvas = router.Add(canvasDesc);
	UiWorldPlacement placement;
	placement.UnitsPerPixel = 0.01f;
	const auto toWorld = CanvasToWorld(UiCanvasMode::WorldPanel, placement, { 400, 200 });
	router.SetWorldPlacement(canvas, toWorld, { 400, 200 });
	const auto bounds = world.GetBounds(button);
	const auto pixel = ProjectCanvasPoint(
		ClipFromCanvas(toWorld, camera), { 800, 600 }, { bounds.X + bounds.Width * 0.5f, bounds.Y + bounds.Height * 0.5f });
	SWIM_REQUIRE(pixel.has_value());

	Input::InputSystem input;
	Frame frame{ input };
	UiInputBridge bridge;
	frame.Mouse(pixel->X, pixel->Y);
	frame.Button(true, pixel->X, pixel->Y);
	input.AdvanceFrame();
	auto result = bridge.Apply(input, router, &camera);
	SWIM_CHECK(result.PointerOverUi);
	SWIM_CHECK(router.GetFocused() == canvas);
	SWIM_CHECK(result.KeyboardCaptured);
	frame.Button(false, pixel->X, pixel->Y);
	input.AdvanceFrame();
	bridge.Apply(input, router, &camera);
	SWIM_CHECK_EQUAL(Count(world.DrainEvents(), UiEventKind::Click, button), 1u);
	// Without a camera only screen canvases can be hit.
	input.AdvanceFrame();
	SWIM_CHECK(!bridge.Apply(input, router).PointerOverUi);
	// Focus loss clears the router's focus.
	Platform::WindowEvent lost{};
	lost.Type = Platform::WindowEventType::FocusLost;
	input.ProcessWindowEvent(lost);
	input.AdvanceFrame();
	bridge.Apply(input, router, &camera);
	SWIM_CHECK(!router.GetFocused());
	SWIM_CHECK(!world.GetFocus());
}

SWIM_TEST("UiInput.Bridge", "RightClickShiftF10AndGamepadNorthOpenContextMenus")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto button = CreateButton(ui, ui.GetRoot(), "Target");
	const auto menu = CreateMenu(ui);
	const auto first = AddMenuItem(ui, menu, "Rename");
	ui.SetContextMenu(button, menu.Root);
	ui.Layout({ 600, 400 });
	const auto bounds = ui.GetBounds(button);

	Input::InputSystem input;
	Frame frame{ input };
	constexpr Platform::InputDeviceId pad = 7;
	UiInputBridgeDesc desc;
	desc.Gamepad = pad;
	UiInputBridge bridge(desc);
	frame.Pad(pad);

	// Right button over the target: the menu opens at the pointer and takes focus.
	const float x = bounds.X + 5.0f;
	const float y = bounds.Y + 5.0f;
	frame.Mouse(x, y);
	frame.RightButton(true, x, y);
	input.AdvanceFrame();
	auto result = bridge.Apply(input, ui);
	SWIM_CHECK(result.ContextMenuOpened);
	SWIM_CHECK(ui.IsPopupOpen(menu.Root));
	ui.Layout({ 600, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(menu.Root).X, x, 1e-3f);
	SWIM_CHECK(ui.GetFocus() == first);
	frame.RightButton(false, x, y);

	// Escape closes; focus falls back to where it was (nothing): press the button first.
	frame.Key(Platform::KeyCode::Escape);
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	frame.Key(Platform::KeyCode::Escape, false);
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root));

	// Shift+F10 opens the focused node's menu below it.
	ui.Layout({ 600, 400 });
	ui.Focus(button);
	frame.Key(Platform::KeyCode::LeftShift);
	frame.Key(Platform::KeyCode::F10);
	input.AdvanceFrame();
	result = bridge.Apply(input, ui);
	SWIM_CHECK(result.ContextMenuOpened);
	SWIM_CHECK_EQUAL(result.KeysConsumed, 1u);
	ui.Layout({ 600, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(menu.Root).Y, bounds.Y + bounds.Height, 1e-3f);
	frame.Key(Platform::KeyCode::F10, false);
	frame.Key(Platform::KeyCode::LeftShift, false);
	ui.CloseAllPopups();
	SWIM_CHECK(ui.GetFocus() == button);

	// Gamepad North does the same; South activates the item and closes the menu.
	frame.PadButton(pad, Platform::GamepadButton::North, true);
	input.AdvanceFrame();
	result = bridge.Apply(input, ui);
	SWIM_CHECK(result.ContextMenuOpened);
	frame.PadButton(pad, Platform::GamepadButton::North, false);
	ui.Layout({ 600, 400 });
	SWIM_CHECK(ui.GetFocus() == first);
	ui.DrainEvents();
	frame.PadButton(pad, Platform::GamepadButton::South, true);
	input.AdvanceFrame();
	bridge.Apply(input, ui);
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, first), 1u);
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root));
	SWIM_CHECK(ui.GetFocus() == button);
}
