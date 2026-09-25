#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

using namespace Swim::UI;

SWIM_TEST("UI.Widgets", "ButtonsFollowHoverPressAndFocusWithoutRelayout")
{
	const auto fonts = Swim::Testing::LoadTextFontChain();
	UiDocument ui;
	UiStyle column;
	column.Gap = 4;
	ui.SetStyle(ui.GetRoot(), column);
	UiButtonColors colors;
	const auto button = CreateButton(ui, ui.GetRoot(), fonts, "Play", 20);
	const auto label = CreateLabel(ui, ui.GetRoot(), fonts, "Score: 0", 16);
	const auto field = CreateTextField(ui, ui.GetRoot(), fonts, 16, {},
		[]
		{
			UiStyle style;
			style.Width = UiLength::Pixels(200);
			style.Height = UiLength::Pixels(24);
			return style;
		}());
	UiImage icon;
	icon.Texture = 5;
	icon.Size = { 24, 24 };
	const auto image = CreateImage(ui, ui.GetRoot(), icon);
	UiStyle scrollStyle;
	scrollStyle.Height = UiLength::Pixels(40);
	const auto scroll = CreateScrollView(ui, ui.GetRoot(), scrollStyle);
	SWIM_CHECK(ui.GetStyle(scroll).Clip);
	SWIM_CHECK(ui.IsEditable(field));
	SWIM_CHECK(!ui.GetStyle(label).HitTest);
	UiButtonStates states;
	states.Track(button, colors);
	ui.Layout({ 400, 400 });
	const auto bounds = ui.GetBounds(button);
	SWIM_CHECK(bounds.Width > 24.0f + 30.0f); // Text plus padding.
	SWIM_CHECK_NEAR(ui.GetBounds(image).Width, 24.0f, 1e-5f);
	const auto revision = ui.GetLayoutRevision();

	ui.PointerMove({ bounds.X + 5, bounds.Y + 5 });
	states.Apply(ui, ui.DrainEvents());
	SWIM_CHECK_NEAR(ui.GetStyle(button).Background.R, colors.Hover.R, 1e-6f);
	ui.PointerDown({ bounds.X + 5, bounds.Y + 5 });
	states.Apply(ui, ui.DrainEvents());
	SWIM_CHECK_NEAR(ui.GetStyle(button).Background.R, colors.Pressed.R, 1e-6f);
	SWIM_CHECK_NEAR(ui.GetStyle(button).BorderWidth, colors.FocusBorder, 1e-6f);
	ui.PointerUp({ 390, 390 });
	states.Apply(ui, ui.DrainEvents());
	SWIM_CHECK_NEAR(ui.GetStyle(button).Background.R, colors.Normal.R, 1e-6f); // Released outside: normal.
	ui.Layout({ 400, 400 });
	SWIM_CHECK_EQUAL(ui.GetLayoutRevision(), revision); // Visual states are paint-only.
	ui.Focus({});
	states.Apply(ui, ui.DrainEvents());
	SWIM_CHECK_NEAR(ui.GetStyle(button).BorderWidth, 0.0f, 1e-6f);
	ui.Remove(button);
	std::vector<UiEvent> stale{ { UiEventKind::Enter, button } };
	states.Apply(ui, stale); // Removed nodes are dropped, not dereferenced.
}
