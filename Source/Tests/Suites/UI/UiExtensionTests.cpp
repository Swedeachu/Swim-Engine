#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

using namespace Swim::UI;

namespace
{

	void WithFonts(UiDocument& ui)
	{
		auto theme = std::make_shared<UiTheme>();
		theme->Fonts = Swim::Testing::LoadTextFontChain();
		ui.SetTheme(theme);
	}

} // namespace

SWIM_TEST("UI.Extensions", "CallbacksAreDeferredIndependentOfPollingAndRemovedWithNodes")
{
	UiDocument ui;
	WithFonts(ui);
	const auto button = CreateButton(ui, ui.GetRoot(), "Remove me");
	int clicks = 0;
	ui.OnClick(button,
		[&]
		{
			++clicks;
			ui.Remove(button);
			ui.DispatchCallbacks(); // Reentry must not dispatch twice.
		});
	ui.Layout({ 300, 100 });
	ui.Focus(button);
	ui.ActivateFocused();
	ui.ActivateFocused();
	SWIM_CHECK_EQUAL(clicks, 0);
	SWIM_CHECK(!ui.DrainEvents().empty());
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(clicks, 1);
	SWIM_CHECK(!ui.Contains(button));
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(clicks, 1);
}

SWIM_TEST("UI.Extensions", "InteractionCallbacksDoNotFireForProgrammaticSetters")
{
	UiDocument ui;
	WithFonts(ui);
	const auto box = CreateCheckbox(ui, ui.GetRoot(), "Enabled");
	int changes = 0;
	bool checked = false;
	ui.OnChecked(box,
		[&](bool value)
		{
			++changes;
			checked = value;
		});
	ui.SetChecked(box, UiCheckState::Checked);
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(changes, 0);
	ui.Layout({ 300, 100 });
	ui.Focus(box);
	ui.ActivateFocused();
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(changes, 1);
	SWIM_CHECK(!checked);
	ui.OnChecked(box, {});
	ui.ActivateFocused();
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(changes, 1);
}

SWIM_TEST("UI.Extensions", "TextCallbackCanReplaceTextAndRemoveItsNode")
{
	UiDocument ui;
	WithFonts(ui);
	const auto field = CreateTextField(ui, ui.GetRoot());
	std::string observed;
	ui.OnText(field,
		[&](const std::string& text)
		{
			ui.Remove(field);
			observed = text; // Text is owned through callback-driven removal.
		});
	ui.Layout({ 300, 100 });
	ui.Focus(field);
	ui.TextInput("hello");
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(observed, std::string("hello"));
	SWIM_CHECK(!ui.Contains(field));
}

namespace
{

	// A gameplay control written against the public API only: drag up/down to change a value,
	// Enter resets it. It never tracks hover/press/focus itself.
	class KnobControl final : public UiControlBehavior
	{

	  public:

		UiPointerResponse OnPointerDown(UiControlContext& context, const UiPointerInput& pointer) override
		{
			grabY = pointer.Position.Y;
			start = context.Control().Value;
			++presses;
			return UiPointerResponse::Capture;
		}

		void OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer) override
		{
			context.ChangeValue(start + (grabY - pointer.Position.Y) * 0.01f, false);
			sawDrag = context.IsDragging();
		}

		void OnPointerUp(UiControlContext& context, bool, bool captured) override
		{
			releasedCaptured = captured;
			context.Commit(start);
		}

		bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers) override
		{
			if (key != UiKey::Enter)
			{
				return false;
			}

			context.ChangeValue(context.Control().Min, true);
			return true;
		}

		float grabY = 0.0f;
		float start = 0.0f;
		int presses = 0;
		bool sawDrag = false;
		bool releasedCaptured = false;

	};

	UiNodeId BuildKnob(UiDocument& document, UiNodeId parent)
	{
		UiStyle style;
		style.Width = UiLength::Pixels(40);
		style.Height = UiLength::Pixels(40);
		const auto node = document.Create(parent);
		document.SetStyle(node, style);
		UiControl control;
		control.Max = 1.0f;
		control.Value = 0.5f;
		document.SetControl(node, "Test.Knob", control);
		return node;
	}

	SWIM_UI_WIDGET(KnobControl, "Test.Knob", BuildKnob);

} // namespace

SWIM_TEST("UI.Extensions", "BuiltInWidgetsAreRegisteredControlsLikeCustomOnes")
{
	UiDocument ui;
	WithFonts(ui);
	auto& registry = UiControlRegistry::Global();

	for (const char* name : { "Button", "Checkbox", "Toggle", "Slider", "ScrollBar", "RadioGroup", "ListView", "Dropdown", "Option" })
	{
		SWIM_CHECK(registry.Contains(name));
	}

	SWIM_CHECK(registry.Contains("Test.Knob")); // Registered from this file by SWIM_UI_WIDGET.
	SWIM_CHECK_THROWS(registry.Register<KnobControl>("Test.Knob"), std::invalid_argument);

	const auto button = CreateButton(ui, ui.GetRoot(), "Play");
	SWIM_CHECK(ui.GetControl(button).Kind == UiControlKind::Button);
	SWIM_CHECK(dynamic_cast<UiButtonControl*>(ui.GetControlBehavior(button)) != nullptr);
	SWIM_CHECK_EQUAL(ui.GetControlBehavior(button)->GetTypeName(), std::string("Button"));

	// Built-in types by name: the same plumbing as gameplay types.
	const auto slider = CreateWidget(ui, ui.GetRoot(), "Slider");
	SWIM_CHECK(ui.GetControl(slider).Kind == UiControlKind::Slider);
	const auto dropdown = CreateDropdown(ui, ui.GetRoot(), UiDropdownDesc{ { "One", "Two" }, 1, {} });
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), 1.0f, 1e-6f);
	SWIM_CHECK_THROWS(CreateWidget(ui, ui.GetRoot(), "Missing.Type"), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.SetControl(ui.Create(ui.GetRoot()), "Missing.Type"), std::invalid_argument);
}

SWIM_TEST("UI.Extensions", "CustomControlsGetGenericInteractionsWithoutTrackingState")
{
	UiDocument ui;
	WithFonts(ui);
	const auto knob = CreateWidget(ui, ui.GetRoot(), "Test.Knob");
	auto* behavior = dynamic_cast<KnobControl*>(ui.GetControlBehavior(knob));
	SWIM_REQUIRE(behavior != nullptr);
	SWIM_CHECK(ui.GetControl(knob).Kind == UiControlKind::Custom);

	std::vector<float> values;
	int commits = 0;
	ui.OnValue(knob, [&](float value) { values.push_back(value); });
	ui.On(knob, UiEventKind::ValueCommitted, [&](const UiEvent&) { ++commits; });
	ui.Layout({ 200, 200 });

	// Press, drag (outside the node: the capture keeps routing), release.
	ui.PointerDown({ 20, 20 });
	ui.PointerMove({ 20, 0 });
	ui.PointerMove({ 120, -20 });
	SWIM_CHECK(HasState(ui.GetState(knob), UiState::Dragging));
	ui.PointerUp({ 120, -20 });
	ui.Update(0.0f);
	SWIM_CHECK_EQUAL(behavior->presses, 1);
	SWIM_CHECK(behavior->sawDrag);
	SWIM_CHECK(behavior->releasedCaptured);
	SWIM_CHECK(ui.GetFocus() == knob.GetNode()); // Focusable by default.
	SWIM_REQUIRE_EQUAL(values.size(), std::size_t{ 2 });
	SWIM_CHECK_NEAR(values.back(), 0.9f, 1e-5f); // 0.5 + 40 px * 0.01.
	SWIM_CHECK_EQUAL(commits, 1);

	// Keys reach the focused control; values clamp through the shared data (Max = 1).
	SWIM_CHECK(ui.KeyDown(UiKey::Enter));
	ui.Update(0.0f);
	SWIM_CHECK_NEAR(ui.GetValue(knob), 0.0f, 1e-6f);
	ui.SetValue(knob, 7.0f);
	SWIM_CHECK_NEAR(ui.GetValue(knob), 1.0f, 1e-6f);

	// An instance attached directly (not registered by name).
	const auto other = ui.Create(ui.GetRoot());
	auto& attached = ui.AttachControl<KnobControl>(other);
	SWIM_CHECK(ui.GetControlBehavior(other) == &attached);
	SWIM_CHECK(attached.GetTypeName().empty());
}

SWIM_TEST("UI.Extensions", "BindingsKeepControlsAndValuesInSyncBothWays")
{
	UiDocument ui;
	WithFonts(ui);
	float exposure = 0.25f;
	bool shadows = true;
	std::uint32_t mode = 2;
	UiSliderDesc desc;
	desc.Max = 4.0f;
	const auto slider = UiWidget(ui, CreateSlider(ui, ui.GetRoot(), desc)).Bind(exposure);
	const auto box = UiWidget(ui, CreateCheckbox(ui, ui.GetRoot(), "Shadows")).Bind(shadows);
	const auto modes = CreateDropdown(ui, ui.GetRoot(), { "A", "B", "C" });
	UiWidget(ui, modes.Root)
		.Bind([&] { return float(mode); }, [&](float value) { mode = std::uint32_t(std::max(value, 0.0f)); });

	// Binding shows the current values at once.
	SWIM_CHECK_NEAR(ui.GetValue(slider), 0.25f, 1e-6f);
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Checked);
	SWIM_CHECK_NEAR(ui.GetValue(modes.Root), 2.0f, 1e-6f);

	// Changed elsewhere (a console command): shown on the next Update, without events.
	int events = 0;
	ui.OnValue(slider, [&](float) { ++events; });
	exposure = 3.0f;
	shadows = false;
	mode = 0;
	ui.Update(0.0f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 3.0f, 1e-6f);
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Unchecked);
	SWIM_CHECK_NEAR(ui.GetValue(modes.Root), 0.0f, 1e-6f);
	SWIM_CHECK_EQUAL(events, 0);

	// Edited by input: written back at dispatch (before handlers run).
	ui.Layout({ 400, 300 });
	ui.Focus(box);
	ui.ActivateFocused();
	ui.Focus(slider);
	ui.KeyDown(UiKey::End);
	ui.Update(0.0f);
	SWIM_CHECK(shadows);
	SWIM_CHECK_NEAR(exposure, 4.0f, 1e-6f);
	SWIM_CHECK_EQUAL(events, 1);

	// The binding goes with its node.
	ui.Remove(slider);
	exposure = 1.0f;
	ui.Update(0.0f);
}

SWIM_TEST("UI.Extensions", "CustomThemeClassesSurviveThemeCopiesAndValidateBeforeReplacement")
{
	UiDocument ui;
	auto theme = std::make_shared<UiTheme>();
	const auto badge = theme->RegisterClass("Game.ScoreBadge",
		[](const UiTheme& theme, UiClassStyle& style)
		{
			style.Style.Background = theme.Palette.Accent;
			style.Style.Width = UiLength::Pixels(30);
		});
	ui.SetTheme(theme);
	const auto node = ui.Create(ui.GetRoot());
	ui.SetThemeClass(node, badge);
	SWIM_CHECK_EQUAL(ui.GetThemeClass(node), badge);
	auto replacement = std::make_shared<UiTheme>(*theme);
	replacement->Palette.Accent = UiSrgbHex(0xff0000);
	ui.SetTheme(replacement);
	SWIM_CHECK_NEAR(ui.GetStyle(node).Background.R, 1.0f, 1e-6f);
	SWIM_CHECK_THROWS(ui.SetTheme(std::make_shared<UiTheme>()), std::invalid_argument);
	SWIM_CHECK(ui.GetTheme() == replacement);
	UiTheme independent;
	const auto sameId = independent.RegisterClass("Game.ScoreBadge",
		[](const UiTheme&, UiClassStyle&)
		{
		});
	SWIM_CHECK_EQUAL(sameId, badge);
	SWIM_CHECK_THROWS(theme->RegisterClass("Game.ScoreBadge",
						  [](const UiTheme&, UiClassStyle&)
						  {
						  }),
		std::invalid_argument);
}
