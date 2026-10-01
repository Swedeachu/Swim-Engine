#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>

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

SWIM_TEST("UI.Extensions", "WidgetsUseTheSameRegistryAsCustomTypedFactories")
{
	UiDocument ui;
	WithFonts(ui);
	SWIM_CHECK(ui.GetWidgets().Contains("Button"));
	const auto button = ui.GetWidgets().Create("Button", ui, ui.GetRoot(), UiButtonDesc{ "Play" });
	SWIM_CHECK(ui.GetControl(button).Kind == UiControlKind::Button);

	struct CounterDesc
	{
		int Value = 0;
	};

	ui.GetWidgets().Register<CounterDesc>("Counter",
		[](UiDocument& document, UiNodeId parent, const CounterDesc& desc)
		{
			return CreateLabel(document, parent, std::to_string(desc.Value));
		});
	const auto counter = CreateWidget(ui, ui.GetRoot(), "Counter", CounterDesc{ 42 });
	SWIM_CHECK_EQUAL(ui.GetText(counter.GetNode()), std::string("42"));
	SWIM_CHECK_THROWS(ui.GetWidgets().Create("Button", ui, ui.GetRoot(), CounterDesc{}), std::invalid_argument);
	const auto dropdown = ui.GetWidgets().Create<UiDropdown>("Dropdown", ui, ui.GetRoot(), UiDropdownDesc{ { "One", "Two" }, 1, {} });
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), 1.0f, 1e-6f);
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
