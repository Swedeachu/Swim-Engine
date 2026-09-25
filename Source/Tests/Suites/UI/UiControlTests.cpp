#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <stdexcept>

using namespace Swim::UI;

namespace
{
	std::shared_ptr<UiTheme> FontTheme()
	{
		auto theme = std::make_shared<UiTheme>();
		theme->Fonts = Swim::Testing::LoadTextFontChain();
		return theme;
	}

	std::vector<UiEvent> Of(const std::vector<UiEvent>& events, UiEventKind kind, UiNodeId node)
	{
		std::vector<UiEvent> result;
		std::copy_if(events.begin(), events.end(), std::back_inserter(result),
			[&](const UiEvent& event)
			{
				return event.Kind == kind && event.Node == node;
			});
		return result;
	}

	UiPoint Center(const UiRect& rect)
	{
		return { rect.X + rect.Width * 0.5f, rect.Y + rect.Height * 0.5f };
	}

	UiStyle Box(float width, float height)
	{
		UiStyle style;
		style.Width = UiLength::Pixels(width);
		style.Height = UiLength::Pixels(height);
		return style;
	}

	std::size_t QuadsOf(const std::vector<UiPaintQuad>& paint, UiNodeId node)
	{
		return std::count_if(paint.begin(), paint.end(),
			[&](const UiPaintQuad& quad)
			{
				return quad.Node == node;
			});
	}
} // namespace

SWIM_TEST("UI.Controls", "CheckboxesToggleByPointerKeysAndActivationAndShowTheirState")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto box = CreateCheckbox(ui, ui.GetRoot(), "Sound");
	const auto& control = ui.GetControl(box);
	SWIM_CHECK(control.Kind == UiControlKind::Checkbox);
	SWIM_REQUIRE(control.Parts.Track && control.Parts.Mark && control.Parts.Mixed && control.Parts.Label);
	const auto mark = control.Parts.Mark;
	const auto mixed = control.Parts.Mixed;
	ui.Layout({ 300, 100 });
	ui.Update(0.0f);
	SWIM_CHECK_NEAR(ui.GetVisual(mark).Opacity, 0.0f, 1e-6f);
	// The label is part of the hit target.
	const auto labelCenter = Center(ui.GetBounds(control.Parts.Label));
	SWIM_CHECK(ui.HitTest(labelCenter) == box);
	ui.PointerDown(labelCenter);
	ui.PointerUp(labelCenter);
	auto events = ui.DrainEvents();
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Checked);
	SWIM_REQUIRE_EQUAL(Of(events, UiEventKind::ValueChanged, box).size(), 1u);
	SWIM_CHECK_NEAR(Of(events, UiEventKind::ValueChanged, box)[0].Value, 1.0f, 1e-6f);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueCommitted, box).size(), 1u);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::Click, box).size(), 1u);
	SWIM_CHECK(ui.GetFocus() == box);
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(mark), UiState::Checked | UiState::Focused));
	SWIM_CHECK_NEAR(ui.GetVisual(mark).Opacity, 1.0f, 1e-6f);
	SWIM_CHECK_NEAR(ui.GetVisual(control.Parts.Track).BorderWidth, ui.GetTheme()->Metrics.FocusWidth, 1e-6f);

	// Space toggles the focused checkbox; a release outside does not.
	SWIM_CHECK(ui.KeyDown(UiKey::Space));
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Unchecked);
	ui.PointerDown(labelCenter);
	ui.PointerUp({ 299, 99 });
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Unchecked);
	ui.DrainEvents();

	// Code changes emit nothing; Mixed shows the mixed mark and a click checks it.
	ui.SetChecked(box, UiCheckState::Mixed);
	SWIM_CHECK(ui.DrainEvents().empty());
	ui.Update(0.0f);
	SWIM_CHECK_NEAR(ui.GetVisual(mixed).Opacity, 1.0f, 1e-6f);
	SWIM_CHECK_NEAR(ui.GetVisual(mark).Opacity, 0.0f, 1e-6f);
	SWIM_CHECK_NEAR(ui.GetValue(box), 0.5f, 1e-6f);
	ui.ActivateFocused();
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Checked);
	SWIM_CHECK_EQUAL(Of(ui.DrainEvents(), UiEventKind::ValueCommitted, box).size(), 1u);

	// Read-only: hoverable and focusable, never changed by input.
	auto readOnly = ui.GetControl(box);
	readOnly.ReadOnly = true;
	ui.SetControl(box, readOnly);
	ui.Layout({ 300, 100 });
	ui.PointerDown(labelCenter);
	ui.PointerUp(labelCenter);
	ui.KeyDown(UiKey::Space);
	SWIM_CHECK(ui.GetChecked(box) == UiCheckState::Checked);
	SWIM_CHECK(HasState(ui.GetState(box), UiState::ReadOnly));
	SWIM_CHECK(Of(ui.DrainEvents(), UiEventKind::ValueChanged, box).empty());

	// Disabled: no hit, no focus.
	auto style = ui.GetStyle(box);
	style.Enabled = false;
	ui.SetStyle(box, style);
	ui.Layout({ 300, 100 });
	SWIM_CHECK(!ui.HitTest(labelCenter));
	SWIM_CHECK(!ui.GetFocus());
	SWIM_CHECK_THROWS(ui.SetChecked(ui.GetRoot(), UiCheckState::Checked), std::invalid_argument);
}

SWIM_TEST("UI.Controls", "TogglesSwitchByClickKnobDragAndEaseTheirKnob")
{
	UiDocument ui;
	auto theme = FontTheme();
	theme->Metrics.TransitionSeconds = 0.1f;
	ui.SetTheme(theme);
	const auto toggle = CreateToggle(ui, ui.GetRoot(), "V-sync");
	const auto track = ui.GetControl(toggle).Parts.Track;
	const auto knob = ui.GetControl(toggle).Parts.Thumb;
	ui.Layout({ 300, 100 });
	const auto trackBounds = ui.GetBounds(track);
	const float inset = theme->Metrics.ToggleInset;
	const float knobSize = theme->Metrics.ToggleHeight - 2.0f * inset;
	const float travel = theme->Metrics.ToggleWidth - 2.0f * inset - knobSize;
	SWIM_CHECK_NEAR(ui.GetBounds(knob).X, trackBounds.X + inset, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetBounds(knob).Width, knobSize, 1e-4f);

	// A click switches on; the knob eases across over the transition.
	ui.PointerDown(Center(trackBounds));
	ui.PointerUp(Center(trackBounds));
	SWIM_CHECK(ui.GetChecked(toggle) == UiCheckState::Checked);
	SWIM_CHECK_EQUAL(Of(ui.DrainEvents(), UiEventKind::ValueCommitted, toggle).size(), 1u);
	SWIM_CHECK(ui.Update(0.05f));
	ui.Layout({ 300, 100 });
	SWIM_CHECK_NEAR(ui.GetBounds(knob).X, trackBounds.X + inset + travel * 0.5f, 1e-3f);
	SWIM_CHECK(!ui.Update(0.1f));
	ui.Layout({ 300, 100 });
	SWIM_CHECK_NEAR(ui.GetBounds(knob).X, trackBounds.X + inset + travel, 1e-4f);

	// Dragging the knob left and releasing past half switches off (no extra toggle).
	const auto knobCenter = Center(ui.GetBounds(knob));
	ui.PointerDown(knobCenter);
	ui.PointerMove({ knobCenter.X - travel * 0.8f, knobCenter.Y });
	ui.Layout({ 300, 100 });
	SWIM_CHECK(HasState(ui.GetState(knob), UiState::Dragging));
	SWIM_CHECK_NEAR(ui.GetBounds(knob).X, trackBounds.X + inset + travel * 0.2f, 1e-3f); // The knob follows.
	ui.PointerUp({ knobCenter.X - travel * 0.8f, knobCenter.Y });
	auto events = ui.DrainEvents();
	SWIM_CHECK(ui.GetChecked(toggle) == UiCheckState::Unchecked);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueChanged, toggle).size(), 1u);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueCommitted, toggle).size(), 1u);
	ui.Update(1.0f);
	ui.Layout({ 300, 100 });
	SWIM_CHECK_NEAR(ui.GetBounds(knob).X, trackBounds.X + inset, 1e-4f);

	// Enter toggles; code changes snap the knob.
	SWIM_CHECK(ui.KeyDown(UiKey::Enter));
	SWIM_CHECK(ui.GetChecked(toggle) == UiCheckState::Checked);
	ui.SetChecked(toggle, UiCheckState::Unchecked);
	SWIM_CHECK(!ui.IsAnimating());
	SWIM_CHECK_THROWS(ui.SetChecked(toggle, UiCheckState::Mixed), std::invalid_argument);
}

SWIM_TEST("UI.Controls", "SlidersDragJumpPageStepAndTakeKeysAndWheelWhileFocused")
{
	UiDocument ui;
	auto theme = FontTheme();
	ui.SetTheme(theme);
	const auto slider = CreateSlider(ui, ui.GetRoot(), { .Min = 0.0f, .Max = 100.0f, .Value = 0.0f, .Step = 5.0f });
	const auto vertical = CreateSlider(ui, ui.GetRoot(), { .Value = 0.25f, .Orientation = UiOrientation::Vertical });
	ui.Layout({ 400, 400 });
	const auto parts = ui.GetControl(slider).Parts;
	const float length = theme->Metrics.SliderLength;
	const float thumb = theme->Metrics.SliderThumb;
	const float travel = length - thumb;
	SWIM_CHECK_NEAR(ui.GetBounds(slider).Width, length, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(parts.Thumb).X, 0.0f, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(parts.Track).Width, length, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(parts.Track).Height, theme->Metrics.SliderTrack, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(parts.Fill).Width, thumb * 0.5f, 1e-5f);

	// Track press: the thumb jumps under the pointer (snapped to the step), then follows.
	const float y = thumb * 0.5f;
	ui.PointerDown({ thumb * 0.5f + 0.26f * travel, y });
	SWIM_CHECK_NEAR(ui.GetValue(slider), 25.0f, 1e-4f);
	SWIM_CHECK(HasState(ui.GetState(parts.Thumb), UiState::Dragging | UiState::Pressed));
	ui.PointerMove({ 1000.0f, 300.0f }); // Far outside: clamps.
	SWIM_CHECK_NEAR(ui.GetValue(slider), 100.0f, 1e-4f);
	ui.PointerUp({ 1000.0f, 300.0f });
	auto events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueChanged, slider).size(), 2u);
	const auto committed = Of(events, UiEventKind::ValueCommitted, slider);
	SWIM_REQUIRE_EQUAL(committed.size(), 1u);
	SWIM_CHECK_NEAR(committed[0].Value, 100.0f, 1e-4f);
	ui.Layout({ 400, 400 }); // Pointer calls already laid out again with the new value.
	SWIM_CHECK_NEAR(ui.GetBounds(parts.Thumb).X, travel, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetBounds(parts.Fill).Width, travel + thumb * 0.5f, 1e-4f);

	// Grabbing the thumb keeps the grab offset.
	ui.PointerDown({ travel + 2.0f, y });
	ui.PointerMove({ travel + 2.0f - 0.5f * travel, y });
	SWIM_CHECK_NEAR(ui.GetValue(slider), 50.0f, 1e-4f);
	ui.PointerUp({ travel + 2.0f - 0.5f * travel, y });
	ui.DrainEvents();

	// Keys while focused: steps, pages (10 % of the range) and ends, each committed.
	SWIM_CHECK(ui.GetFocus() == slider);
	SWIM_CHECK(ui.KeyDown(UiKey::Right));
	SWIM_CHECK_NEAR(ui.GetValue(slider), 55.0f, 1e-4f);
	SWIM_CHECK(ui.KeyDown(UiKey::PageDown));
	SWIM_CHECK_NEAR(ui.GetValue(slider), 45.0f, 1e-4f);
	SWIM_CHECK(ui.KeyDown(UiKey::End));
	SWIM_CHECK_NEAR(ui.GetValue(slider), 100.0f, 1e-4f);
	SWIM_CHECK(ui.KeyDown(UiKey::Home));
	SWIM_CHECK_NEAR(ui.GetValue(slider), 0.0f, 1e-4f);
	SWIM_CHECK_EQUAL(Of(ui.DrainEvents(), UiEventKind::ValueCommitted, slider).size(), 4u);
	// The cross axis navigates instead (down to the vertical slider).
	SWIM_CHECK(ui.KeyDown(UiKey::Down));
	SWIM_CHECK(ui.GetFocus() == vertical);
	SWIM_CHECK(ui.KeyDown(UiKey::Up)); // Vertical sliders use Up/Down.
	SWIM_CHECK_NEAR(ui.GetValue(vertical), 0.26f, 1e-5f);
	ui.Focus(slider);

	// Wheel over the focused slider steps it; unfocused sliders let the wheel through.
	ui.Layout({ 400, 400 });
	const auto verticalCenter = Center(ui.GetBounds(vertical));
	SWIM_CHECK(ui.Wheel({ 10.0f, y }, { 0.0f, -48.0f }));
	SWIM_CHECK_NEAR(ui.GetValue(slider), 5.0f, 1e-4f);
	SWIM_CHECK(!ui.Wheel(verticalCenter, { 0.0f, -48.0f }));

	// Vertical sliders put Min at the bottom.
	ui.Layout({ 400, 400 });
	const auto verticalThumb = ui.GetControl(vertical).Parts.Thumb;
	const auto verticalBounds = ui.GetBounds(vertical);
	SWIM_CHECK_NEAR(ui.GetBounds(verticalThumb).Y, verticalBounds.Y + (1.0f - 0.26f) * travel, 1e-3f);

	// Page track clicks, SetValue from code (no events), validation.
	auto paged = ui.GetControl(slider);
	paged.TrackClick = UiTrackClick::Page;
	paged.PageStep = 20.0f;
	ui.SetControl(slider, paged);
	ui.Layout({ 400, 400 });
	ui.PointerDown({ length - 2.0f, y });
	ui.PointerUp({ length - 2.0f, y });
	SWIM_CHECK_NEAR(ui.GetValue(slider), 25.0f, 1e-4f);
	ui.DrainEvents();
	ui.SetValue(slider, 62.0f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 60.0f, 1e-4f); // Snapped to the step.
	ui.SetValue(slider, 1e9f);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 100.0f, 1e-4f);
	SWIM_CHECK(ui.DrainEvents().empty());
	auto invalid = paged;
	invalid.Min = 5.0f;
	invalid.Max = 1.0f;
	SWIM_CHECK_THROWS(ui.SetControl(slider, invalid), std::invalid_argument);
	invalid = paged;
	invalid.Parts.Thumb = vertical; // Not a descendant.
	SWIM_CHECK_THROWS(ui.SetControl(slider, invalid), std::invalid_argument);
	invalid = paged;
	invalid.Step = -1.0f;
	SWIM_CHECK_THROWS(ui.SetControl(slider, invalid), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.SetValue(ui.GetRoot(), 1.0f), std::invalid_argument);
}

SWIM_TEST("UI.Controls", "ScrollBarsFollowTheirTargetScrollItAndHideWithoutOverflow")
{
	UiDocument ui;
	auto theme = FontTheme();
	ui.SetTheme(theme);
	const auto area = CreateScrollArea(ui, ui.GetRoot(), Box(100, 50));
	const auto content = ui.Create(area.Viewport);
	ui.SetStyle(content, Box(80, 200));
	ui.Layout({ 300, 300 });
	const float thickness = theme->Metrics.ScrollBarThickness;
	const auto bar = area.Vertical;
	const auto thumb = ui.GetControl(bar).Parts.Thumb;
	SWIM_CHECK_NEAR(ui.GetBounds(area.Viewport).Width, 100.0f - thickness, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetBounds(bar).Width, thickness, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetBounds(bar).Height, 50.0f, 1e-4f);
	// Thumb: 50 * 50 / 200 = 12.5, raised to the theme's minimum.
	const float thumbLength = theme->Metrics.ScrollBarMinThumb;
	SWIM_CHECK_NEAR(ui.GetBounds(thumb).Height, thumbLength, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetBounds(thumb).Y, 0.0f, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetControl(bar).Max, 150.0f, 1e-4f);

	// Scrolling from code moves the thumb in the same Layout, even though the bar is
	// arranged before... or after its target.
	ui.SetScroll(area.Viewport, { 0, 75 });
	ui.Layout({ 300, 300 });
	SWIM_CHECK_NEAR(ui.GetValue(bar), 75.0f, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetBounds(thumb).Y, 0.5f * (50.0f - thumbLength), 1e-4f);

	// Dragging the thumb scrolls the target.
	const auto grab = Center(ui.GetBounds(thumb));
	ui.PointerDown(grab);
	ui.PointerMove({ grab.X, grab.Y + 50.0f });
	SWIM_CHECK_NEAR(ui.GetScroll(area.Viewport).Y, 150.0f, 1e-4f);
	ui.PointerUp({ grab.X, grab.Y + 50.0f });
	auto events = ui.DrainEvents();
	SWIM_CHECK(!Of(events, UiEventKind::ValueChanged, bar).empty());
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueCommitted, bar).size(), 1u);
	SWIM_CHECK(!ui.GetFocus()); // Scroll bars are not focusable by default.
	ui.Layout({ 300, 300 });
	SWIM_CHECK_NEAR(ui.GetBounds(thumb).Y, 50.0f - thumbLength, 1e-4f);

	// A track press pages by the viewport; the wheel over the bar scrolls too.
	const float barX = ui.GetBounds(bar).X + thickness * 0.5f;
	ui.PointerDown({ barX, 2.0f });
	ui.PointerUp({ barX, 2.0f });
	SWIM_CHECK_NEAR(ui.GetScroll(area.Viewport).Y, 100.0f, 1e-4f);
	ui.Layout({ 300, 300 });
	SWIM_CHECK(ui.Wheel({ barX, 2.0f }, { 0.0f, 20.0f }));
	SWIM_CHECK_NEAR(ui.GetScroll(area.Viewport).Y, 120.0f, 1e-4f);
	ui.SetValue(bar, 10.0f);
	ui.Layout({ 300, 300 });
	SWIM_CHECK_NEAR(ui.GetScroll(area.Viewport).Y, 10.0f, 1e-4f);

	// Without overflow an Auto bar is neither painted nor hit (it keeps its space).
	Swim::Text::GlyphAtlas atlas;
	SWIM_CHECK(QuadsOf(ui.Paint(atlas), thumb) == 1u);
	ui.SetStyle(content, Box(80, 30));
	ui.Layout({ 300, 300 });
	SWIM_CHECK(ui.HitTest({ barX, 10.0f }) != bar);
	const auto& paint = ui.Paint(atlas);
	SWIM_CHECK_EQUAL(QuadsOf(paint, bar) + QuadsOf(paint, thumb), 0u);
	SWIM_CHECK_NEAR(ui.GetBounds(area.Viewport).Width, 100.0f - thickness, 1e-4f);

	// A bar's target must be outside the bar.
	auto invalid = ui.GetControl(bar);
	invalid.ScrollTarget = thumb;
	SWIM_CHECK_THROWS(ui.SetControl(bar, invalid), std::invalid_argument);
}

SWIM_TEST("UI.Controls", "OverlayScrollBarsFloatOverContentAndFadeAfterInactivity")
{
	UiDocument ui;
	auto theme = FontTheme();
	ui.SetTheme(theme);
	const auto area = CreateScrollArea(ui, ui.GetRoot(), Box(100, 50), true, true, UiScrollBarVisibility::Overlay);
	const auto content = ui.Create(area.Viewport);
	ui.SetStyle(content, Box(300, 200));
	ui.Layout({ 300, 300 });
	SWIM_CHECK_NEAR(ui.GetBounds(area.Viewport).Width, 100.0f, 1e-4f); // Bars float.
	const auto vertical = ui.GetBounds(area.Vertical);
	const auto horizontal = ui.GetBounds(area.Horizontal);
	SWIM_CHECK_NEAR(vertical.X + vertical.Width, 100.0f, 1e-4f);
	SWIM_CHECK_NEAR(horizontal.Y + horizontal.Height, 50.0f, 1e-4f);
	SWIM_CHECK_NEAR(ui.GetControl(area.Horizontal).Max, 200.0f, 1e-4f);
	Swim::Text::GlyphAtlas atlas;
	SWIM_CHECK(QuadsOf(ui.Paint(atlas), area.Vertical) > 0u);
	SWIM_CHECK(ui.Update(0.5f)); // Still within the fade delay.
	ui.Update(1.0f);
	SWIM_CHECK(!ui.IsAnimating());
	SWIM_CHECK_EQUAL(QuadsOf(ui.Paint(atlas), area.Vertical), 0u);
	// Scrolling (wheel over the content) brings the bars back.
	SWIM_CHECK(ui.Wheel({ 40.0f, 20.0f }, { 0.0f, 30.0f }));
	ui.Layout({ 300, 300 });
	ui.Update(0.0f);
	SWIM_CHECK(ui.IsAnimating());
	SWIM_CHECK(QuadsOf(ui.Paint(atlas), area.Vertical) > 0u);
}

SWIM_TEST("UI.Navigation", "TabIndexOrdersFocusAndNegativeIndicesAreSkipped")
{
	UiDocument ui;
	std::vector<UiNodeId> nodes;
	for (const int index : { 0, 2, -1, 1, 0 })
	{
		auto style = Box(20, 20);
		style.Focusable = true;
		style.HitTest = true;
		style.TabIndex = index;
		nodes.push_back(ui.Create(ui.GetRoot()));
		ui.SetStyle(nodes.back(), style);
	}
	ui.Layout({ 100, 200 });
	const std::vector<UiNodeId> expected{ nodes[3], nodes[1], nodes[0], nodes[4] };
	for (const auto node : expected)
	{
		ui.FocusNext();
		SWIM_CHECK(ui.GetFocus() == node);
	}
	ui.FocusNext();
	SWIM_CHECK(ui.GetFocus() == nodes[3]); // Wraps.
	ui.FocusNext(true);
	SWIM_CHECK(ui.GetFocus() == nodes[4]);
	// Negative indices stay focusable by pointer.
	ui.PointerDown(Center(ui.GetBounds(nodes[2])));
	SWIM_CHECK(ui.GetFocus() == nodes[2]);
}

SWIM_TEST("UI.Navigation", "DirectionsMoveFocusSpatiallyAndControlsKeepTheirAxis")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	auto grid = ui.GetStyle(ui.GetRoot());
	grid.Gap = 10;
	ui.SetStyle(ui.GetRoot(), grid);
	UiStyle row;
	row.Flow = UiFlow::Row;
	row.Gap = 10;
	std::vector<std::vector<UiNodeId>> cells(2);
	for (auto& line : cells)
	{
		const auto container = ui.Create(ui.GetRoot());
		ui.SetStyle(container, row);
		for (int i = 0; i < 3; ++i)
		{
			line.push_back(CreateButton(ui, container, "Cell"));
		}
	}
	const auto field = CreateTextField(ui, ui.GetRoot());
	ui.Layout({ 600, 400 });
	SWIM_CHECK(ui.Navigate(UiNavDirection::Down)); // No focus: the first in tab order.
	SWIM_CHECK(ui.GetFocus() == cells[0][0]);
	SWIM_CHECK(ui.Navigate(UiNavDirection::Right));
	SWIM_CHECK(ui.GetFocus() == cells[0][1]);
	SWIM_CHECK(ui.KeyDown(UiKey::Down)); // Arrows navigate from buttons.
	SWIM_CHECK(ui.GetFocus() == cells[1][1]);
	SWIM_CHECK(ui.Navigate(UiNavDirection::Right));
	SWIM_CHECK(ui.Navigate(UiNavDirection::Up));
	SWIM_CHECK(ui.GetFocus() == cells[0][2]);
	SWIM_CHECK(!ui.Navigate(UiNavDirection::Right)); // Nothing that way: no wrap-around.
	SWIM_CHECK(!ui.Navigate(UiNavDirection::Up));
	SWIM_CHECK(ui.GetFocus() == cells[0][2]);
	ui.Focus(cells[1][0]);
	SWIM_CHECK(ui.Navigate(UiNavDirection::Down));
	SWIM_CHECK(ui.GetFocus() == field);
	// Text fields keep Left/Right for their caret.
	ui.TextInput("ab");
	SWIM_CHECK(ui.KeyDown(UiKey::Left));
	SWIM_CHECK(ui.GetFocus() == field);
	SWIM_CHECK_EQUAL(ui.GetSelection(field).Caret, 1u);
	// Arrow navigation can be turned off.
	ui.SetArrowNavigation(false);
	ui.Focus(cells[0][0]);
	SWIM_CHECK(!ui.KeyDown(UiKey::Right));
	SWIM_CHECK(ui.GetFocus() == cells[0][0]);
	SWIM_CHECK(ui.Navigate(UiNavDirection::Right)); // Explicit navigation still works.
}

SWIM_TEST("UI.Controls", "SliderValueLabelsTickMarksAndScrollBarStepButtonsThatRepeat")
{
	UiDocument ui;
	auto theme = FontTheme();
	ui.SetTheme(theme);
	const auto slider = CreateSlider(
		ui, ui.GetRoot(), { .Min = 0.0f, .Max = 10.0f, .Value = 3.0f, .Step = 1.0f, .Ticks = 3, .ShowValue = true, .Decimals = 1 });
	const auto label = ui.GetControl(slider).Parts.Label;
	SWIM_REQUIRE(static_cast<bool>(label));
	SWIM_CHECK_EQUAL(ui.GetText(label), std::string("3.0"));
	SWIM_CHECK(ui.GetThemeClass(label) == UiThemeClass::SliderValue);
	ui.Layout({ 400, 300 });
	const auto sliderBounds = ui.GetBounds(slider);
	SWIM_CHECK(ui.GetBounds(label).X >= sliderBounds.X + sliderBounds.Width); // Beside it, in a row.
	// Ticks at 0, 5 and 10, centred on the thumb positions of those values.
	const float thumb = theme->Metrics.SliderThumb;
	const float travel = theme->Metrics.SliderLength - thumb;
	std::vector<float> ticks;
	UiState tickState = UiState::None;
	for (auto node = UiNodeId{ slider.Value + 1 }; ui.Contains(node) && ticks.size() < 8; node = UiNodeId{ node.Value + 1 })
	{
		if (ui.GetThemeClass(node) == UiThemeClass::SliderTick)
		{
			const auto bounds = ui.GetBounds(node);
			ticks.push_back(bounds.X + bounds.Width * 0.5f - sliderBounds.X);
			tickState = ui.GetState(node);
		}
	}
	SWIM_REQUIRE_EQUAL(ticks.size(), std::size_t(3));
	for (std::size_t i = 0; i < ticks.size(); ++i)
	{
		SWIM_CHECK_NEAR(ticks[i], thumb * 0.5f + travel * 0.5f * float(i), 1e-4f);
	}
	SWIM_CHECK(!HasState(tickState, UiState::Hovered));
	// Input and code both update the label.
	ui.Focus(slider);
	SWIM_CHECK(ui.KeyDown(UiKey::Right));
	SWIM_CHECK_EQUAL(ui.GetText(label), std::string("4.0"));
	ui.SetValue(slider, 7.26f);
	SWIM_CHECK_EQUAL(ui.GetText(label), std::string("7.0"));
	SWIM_CHECK_THROWS(ui.SetPartRole(label, slider, UiPartRole::Tick, 1.0f), std::invalid_argument); // Not a child.

	// Scroll bar step buttons: a step per press, repeating while held.
	UiDocument list;
	list.SetTheme(theme);
	const auto area = CreateScrollArea(list, list.GetRoot(), Box(100, 100), true, false, UiScrollBarVisibility::Auto, true);
	const auto content = list.Create(area.Viewport);
	list.SetStyle(content, Box(80, 400));
	list.Layout({ 300, 300 });
	const auto bar = area.Vertical;
	const auto parts = list.GetControl(bar).Parts;
	SWIM_REQUIRE(parts.Decrement && parts.Increment);
	const float thickness = theme->Metrics.ScrollBarThickness;
	SWIM_CHECK_NEAR(list.GetBounds(parts.Decrement).Height, thickness, 1e-4f);
	SWIM_CHECK_NEAR(list.GetBounds(parts.Increment).Y, 100.0f - thickness, 1e-4f);
	SWIM_CHECK_NEAR(list.GetBounds(parts.Thumb).Y, thickness, 1e-4f); // The track starts after the button.
	const float x = list.GetBounds(bar).X + thickness * 0.5f;
	list.PointerDown({ x, 5.0f });
	list.PointerUp({ x, 5.0f });
	SWIM_CHECK_NEAR(list.GetScroll(area.Viewport).Y, 0.0f, 1e-4f); // Already at the start.
	list.PointerDown({ x, 95.0f });
	SWIM_CHECK_NEAR(list.GetScroll(area.Viewport).Y, 40.0f, 1e-4f);
	SWIM_CHECK(list.Update(0.3f));
	SWIM_CHECK_NEAR(list.GetScroll(area.Viewport).Y, 40.0f, 1e-4f); // Within the repeat delay.
	list.Update(0.12f);												// Held 0.42 s: the first repeat (at 0.4 s).
	SWIM_CHECK_NEAR(list.GetScroll(area.Viewport).Y, 80.0f, 1e-4f);
	list.Update(0.12f); // Held 0.54 s: repeats at 0.45 and 0.5 s.
	SWIM_CHECK_NEAR(list.GetScroll(area.Viewport).Y, 160.0f, 1e-4f);
	list.PointerUp({ x, 95.0f });
	list.Update(1.0f);
	list.Layout({ 300, 300 });
	SWIM_CHECK_NEAR(list.GetScroll(area.Viewport).Y, 160.0f, 1e-4f);
	SWIM_CHECK_NEAR(list.GetBounds(parts.Thumb).Y, thickness + 160.0f / 300.0f * (100.0f - 2.0f * thickness - 24.0f), 1e-3f);
	SWIM_CHECK_EQUAL(list.DrainEvents().size() > 0, true);
}
