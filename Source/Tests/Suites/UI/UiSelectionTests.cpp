#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <stdexcept>
#include <string>

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

	void Click(UiDocument& ui, UiPoint point)
	{
		ui.PointerDown(point);
		ui.PointerUp(point);
	}

	UiStyle Box(float width, float height)
	{
		UiStyle style;
		style.Width = UiLength::Pixels(width);
		style.Height = UiLength::Pixels(height);
		return style;
	}
} // namespace

SWIM_TEST("UI.Selection", "RadioGroupsSelectByPointerAndWrappingArrowsAndShowTheChoice")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto group = CreateRadioGroup(ui, ui.GetRoot(), { "Low", "Medium", "High" }, 1);
	SWIM_CHECK_EQUAL(ui.GetOptionCount(group), 3u);
	SWIM_CHECK_NEAR(ui.GetValue(group), 1.0f, 0.0f);
	const auto low = ui.FindOption(group, 0);
	const auto high = ui.FindOption(group, 2);
	SWIM_REQUIRE(low && high && !ui.FindOption(group, 3));
	ui.Layout({ 400, 300 });
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(ui.FindOption(group, 1)), UiState::Checked));
	SWIM_CHECK(!HasState(ui.GetState(low), UiState::Checked));

	// A click on an option selects it and focuses the group (options are never focused).
	SWIM_CHECK(ui.HitTest(Center(ui.GetBounds(high))) == high);
	Click(ui, Center(ui.GetBounds(high)));
	auto events = ui.DrainEvents();
	SWIM_CHECK(ui.GetFocus() == group);
	SWIM_CHECK_NEAR(ui.GetValue(group), 2.0f, 0.0f);
	SWIM_REQUIRE_EQUAL(Of(events, UiEventKind::ValueChanged, group).size(), 1u);
	SWIM_CHECK_NEAR(Of(events, UiEventKind::ValueChanged, group)[0].Value, 2.0f, 0.0f);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueCommitted, group).size(), 1u);
	ui.Update(0.0f);
	// The selected option shows the group's focus; the dot (a part) follows its option.
	SWIM_CHECK(HasState(ui.GetState(high), UiState::Checked | UiState::Focused));
	SWIM_CHECK(!HasState(ui.GetState(low), UiState::Focused));

	// Arrows wrap; Home/End jump; they never leave the group.
	SWIM_CHECK(ui.KeyDown(UiKey::Down));
	SWIM_CHECK_NEAR(ui.GetValue(group), 0.0f, 0.0f);
	SWIM_CHECK(ui.KeyDown(UiKey::Up));
	SWIM_CHECK_NEAR(ui.GetValue(group), 2.0f, 0.0f);
	SWIM_CHECK(ui.KeyDown(UiKey::Home));
	SWIM_CHECK_NEAR(ui.GetValue(group), 0.0f, 0.0f);
	SWIM_CHECK(ui.GetFocus() == group);
	ui.DrainEvents();

	// Clicking the selected option changes nothing.
	ui.Layout({ 400, 300 });
	Click(ui, Center(ui.GetBounds(low)));
	SWIM_CHECK(Of(ui.DrainEvents(), UiEventKind::ValueChanged, group).empty());

	// From code: no events, clamped to the options.
	ui.SetValue(group, 7.0f);
	SWIM_CHECK_NEAR(ui.GetValue(group), 2.0f, 0.0f);
	ui.SetValue(group, -5.0f);
	SWIM_CHECK_NEAR(ui.GetValue(group), -1.0f, 0.0f);
	SWIM_CHECK(ui.DrainEvents().empty());

	// Read-only groups ignore input.
	auto control = ui.GetControl(group);
	control.ReadOnly = true;
	ui.SetControl(group, control);
	ui.KeyDown(UiKey::Down);
	SWIM_CHECK_NEAR(ui.GetValue(group), -1.0f, 0.0f);

	// Registration rules.
	const auto stray = ui.Create(ui.GetRoot());
	SWIM_CHECK_THROWS(ui.SetPartRole(stray, group, UiPartRole::Option, 3.0f), std::invalid_argument); // Outside the group.
	const auto inside = ui.Create(group);
	SWIM_CHECK_THROWS(ui.SetPartRole(inside, group, UiPartRole::Option, 1.5f), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.SetPartRole(inside, group, UiPartRole::Option, -1.0f), std::invalid_argument);
	const auto button = CreateButton(ui, group, "x");
	SWIM_CHECK_THROWS(ui.SetPartRole(button, group, UiPartRole::Option, 3.0f), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.SetPartRole(inside, button, UiPartRole::Option, 0.0f), std::invalid_argument);
	ui.SetPartRole(inside, group, UiPartRole::Option, 3.0f);
	SWIM_CHECK_EQUAL(ui.GetOptionCount(group), 4u);
	SWIM_CHECK(ui.GetControl(inside).Kind == UiControlKind::Option);
	ui.SetPartRole(inside, {}, UiPartRole::None);
	SWIM_CHECK_EQUAL(ui.GetOptionCount(group), 3u);
	SWIM_CHECK(ui.GetControl(inside).Kind == UiControlKind::None);
	UiControl bad;
	bad.Kind = UiControlKind::RadioGroup;
	bad.Value = 0.5f;
	SWIM_CHECK_THROWS(ui.SetControl(stray, bad), std::invalid_argument);
	bad.Kind = UiControlKind::Option;
	bad.Value = 0.0f;
	SWIM_CHECK_THROWS(ui.SetControl(stray, bad), std::invalid_argument); // Options come from SetPartRole.
}

SWIM_TEST("UI.Selection", "ListViewsSelectRevealAndSubmitWithKeysAndScrollBars")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	std::vector<std::string> items;
	for (int i = 0; i < 30; ++i)
	{
		items.push_back("Item " + std::to_string(i));
	}
	const auto list = CreateListView(ui, ui.GetRoot(), Box(200, 140), items);
	SWIM_CHECK_EQUAL(ui.GetOptionCount(list.Root), 30u);
	SWIM_CHECK_NEAR(ui.GetValue(list.Root), -1.0f, 0.0f);
	ui.Layout({ 400, 400 });
	const auto rowHeight = ui.GetBounds(ui.FindOption(list.Root, 0)).Height;
	SWIM_REQUIRE(rowHeight > 0.0f);
	const auto viewport = ui.GetBounds(list.Viewport);

	Click(ui, Center(ui.GetBounds(ui.FindOption(list.Root, 1))));
	SWIM_CHECK(ui.GetFocus() == list.Root);
	SWIM_CHECK_NEAR(ui.GetValue(list.Root), 1.0f, 0.0f);
	ui.DrainEvents();

	// Down past the viewport scrolls the selection into view.
	for (int i = 0; i < 8; ++i)
	{
		SWIM_CHECK(ui.KeyDown(UiKey::Down));
	}
	SWIM_CHECK_NEAR(ui.GetValue(list.Root), 9.0f, 0.0f);
	ui.Layout({ 400, 400 });
	const auto nine = ui.GetBounds(ui.FindOption(list.Root, 9));
	SWIM_CHECK(ui.GetScroll(list.Viewport).Y > 0.0f);
	SWIM_CHECK(nine.Y >= viewport.Y - 0.01f);
	SWIM_CHECK(nine.Y + nine.Height <= viewport.Y + viewport.Height + 0.01f);

	// Page keys move by the rows that fit; End/Home reach the ends.
	const auto pageRows = static_cast<int>(viewport.Height / rowHeight);
	SWIM_CHECK(ui.KeyDown(UiKey::PageDown));
	SWIM_CHECK_NEAR(ui.GetValue(list.Root), float(9 + pageRows), 0.0f);
	SWIM_CHECK(ui.KeyDown(UiKey::End));
	SWIM_CHECK_NEAR(ui.GetValue(list.Root), 29.0f, 0.0f);
	ui.Layout({ 400, 400 });
	const auto last = ui.GetBounds(ui.FindOption(list.Root, 29));
	SWIM_CHECK(last.Y + last.Height <= viewport.Y + viewport.Height + 0.01f);
	SWIM_CHECK(ui.KeyDown(UiKey::Home));
	SWIM_CHECK_NEAR(ui.GetValue(list.Root), 0.0f, 0.0f);
	ui.Layout({ 400, 400 });
	SWIM_CHECK_NEAR(ui.GetScroll(list.Viewport).Y, 0.0f, 1e-3f);
	ui.DrainEvents();

	// Enter submits the selection.
	SWIM_CHECK(ui.KeyDown(UiKey::Enter));
	auto events = ui.DrainEvents();
	SWIM_REQUIRE_EQUAL(Of(events, UiEventKind::Submit, list.Root).size(), 1u);
	SWIM_CHECK_NEAR(Of(events, UiEventKind::Submit, list.Root)[0].Value, 0.0f, 0.0f);

	// Pressing the list's scroll bar keeps the list focused (a part of a focusable node).
	SWIM_REQUIRE(list.ScrollBar);
	const auto bar = Center(ui.GetBounds(list.ScrollBar));
	ui.PointerDown(bar);
	ui.PointerUp(bar);
	SWIM_CHECK(ui.GetFocus() == list.Root);
}

SWIM_TEST("UI.Selection", "DropdownsOpenBelowHighlightWithKeysAndCommitTheChoice")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto other = CreateButton(ui, ui.GetRoot(), "Other");
	const auto dropdown = CreateDropdown(ui, ui.GetRoot(), { "Windowed", "Borderless", "Fullscreen" }, -1, "Mode");
	SWIM_CHECK(ui.GetText(dropdown.Label) == "Mode"); // The placeholder until something is chosen.
	ui.Layout({ 500, 400 });
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));
	SWIM_CHECK(ui.GetBounds(dropdown.List.Root).Width == 0.0f); // Closed popups are not laid out.

	// A click opens the list below the dropdown, at least as wide.
	Click(ui, Center(ui.GetBounds(dropdown.Root)));
	auto events = ui.DrainEvents();
	SWIM_CHECK(ui.IsPopupOpen(dropdown.List.Root));
	SWIM_CHECK(ui.GetTopPopup() == dropdown.List.Root);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::PopupOpened, dropdown.List.Root).size(), 1u);
	SWIM_CHECK(ui.GetFocus() == dropdown.Root);
	ui.Layout({ 500, 400 });
	const auto anchor = ui.GetBounds(dropdown.Root);
	const auto popup = ui.GetBounds(dropdown.List.Root);
	SWIM_CHECK_NEAR(popup.Y, anchor.Y + anchor.Height, 1e-3f);
	SWIM_CHECK_NEAR(popup.X, anchor.X, 1e-3f);
	SWIM_CHECK(popup.Width >= anchor.Width - 1e-3f);

	// Keys move the highlight (Focused on the option) without selecting; Enter commits.
	ui.Update(0.0f);
	const auto first = ui.FindOption(dropdown.Root, 0);
	const auto second = ui.FindOption(dropdown.Root, 1);
	SWIM_CHECK(HasState(ui.GetState(first), UiState::Focused));
	SWIM_CHECK(ui.KeyDown(UiKey::Down));
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(second), UiState::Focused));
	SWIM_CHECK(!HasState(ui.GetState(first), UiState::Focused));
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), -1.0f, 0.0f);
	SWIM_CHECK(ui.KeyDown(UiKey::Enter));
	events = ui.DrainEvents();
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), 1.0f, 0.0f);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueCommitted, dropdown.Root).size(), 1u);
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::PopupClosed, dropdown.List.Root).size(), 1u);
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));
	SWIM_CHECK(ui.GetText(dropdown.Label) == "Borderless");
	SWIM_CHECK(ui.GetFocus() == dropdown.Root);

	// Closed: Up/Down select directly.
	SWIM_CHECK(ui.KeyDown(UiKey::Down));
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), 2.0f, 0.0f);
	SWIM_CHECK(ui.GetText(dropdown.Label) == "Fullscreen");
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));

	// Open with Space, hover moves the highlight, a click on an option commits and closes.
	SWIM_CHECK(ui.KeyDown(UiKey::Space));
	ui.Layout({ 500, 400 });
	const auto zero = Center(ui.GetBounds(first));
	ui.PointerMove(zero);
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(first), UiState::Focused | UiState::Hovered));
	Click(ui, zero);
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), 0.0f, 0.0f);
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));
	SWIM_CHECK(ui.GetFocus() == dropdown.Root);

	// Escape closes without changing the value.
	ui.KeyDown(UiKey::Enter);
	ui.KeyDown(UiKey::Down);
	SWIM_CHECK(ui.KeyDown(UiKey::Escape));
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));
	SWIM_CHECK_NEAR(ui.GetValue(dropdown.Root), 0.0f, 0.0f);
	SWIM_CHECK(ui.GetFocus() == dropdown.Root);

	// A press outside closes it (light dismiss) and still reaches what was pressed.
	ui.KeyDown(UiKey::Enter);
	ui.Layout({ 500, 400 });
	ui.DrainEvents();
	Click(ui, Center(ui.GetBounds(other)));
	events = ui.DrainEvents();
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::Click, other).size(), 1u);
	SWIM_CHECK(ui.GetFocus() == other);

	// Tab away closes an open list.
	ui.Focus(dropdown.Root);
	ui.KeyDown(UiKey::Enter);
	SWIM_CHECK(ui.IsPopupOpen(dropdown.List.Root));
	ui.KeyDown(UiKey::Tab);
	SWIM_CHECK(!ui.IsPopupOpen(dropdown.List.Root));

	// The popup must be a root child.
	auto control = ui.GetControl(dropdown.Root);
	control.Parts.Popup = dropdown.Label;
	SWIM_CHECK_THROWS(ui.SetControl(dropdown.Root, control), std::invalid_argument);
}

SWIM_TEST("UI.Selection", "LongDropdownListsScrollTheHighlightIntoView")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	std::vector<std::string> options;
	for (int i = 0; i < 40; ++i)
	{
		options.push_back("Option " + std::to_string(i));
	}
	const auto dropdown = CreateDropdown(ui, ui.GetRoot(), options, 35);
	ui.Layout({ 400, 1000 });
	ui.ActivateFocused(); // Nothing focused: no-op.
	ui.Focus(dropdown.Root);
	ui.ActivateFocused();
	ui.Layout({ 400, 1000 });
	const auto popup = ui.GetBounds(dropdown.List.Root);
	SWIM_CHECK(popup.Height <= ui.GetTheme()->Metrics.PopupMaxHeight + 1e-3f);
	// The selected option was revealed on open.
	const auto items = ui.GetBounds(dropdown.List.Items);
	const auto selected = ui.GetBounds(ui.FindOption(dropdown.Root, 35));
	SWIM_CHECK(ui.GetScroll(dropdown.List.Items).Y > 0.0f);
	SWIM_CHECK(selected.Y >= items.Y - 0.01f);
	SWIM_CHECK(selected.Y + selected.Height <= items.Y + items.Height + 0.01f);
	SWIM_CHECK(ui.KeyDown(UiKey::End));
	ui.Layout({ 400, 1000 });
	const auto end = ui.GetBounds(ui.FindOption(dropdown.Root, 39));
	SWIM_CHECK(end.Y + end.Height <= items.Y + items.Height + 0.01f);
}

SWIM_TEST("UI.Selection", "VirtualListsBindOnlyVisibleRowsAndKeepSelectionAcrossRebinds")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	auto fonts = ui.GetTheme()->Fonts;
	UiVirtualListDesc desc;
	desc.ItemCount = 10000;
	desc.ItemHeight = 20.0f;
	desc.Style = Box(200, 200);
	desc.Overscan = 1;
	desc.Bind = [&](UiDocument& document, UiNodeId row, std::uint32_t index)
	{
		document.SetText(row, fonts, "Row " + std::to_string(index), 14.0f);
	};
	UiVirtualList list(ui, ui.GetRoot(), desc);
	SWIM_CHECK_EQUAL(ui.GetOptionCount(list.GetRoot()), 10000u);
	ui.Layout({ 400, 400 });
	SWIM_CHECK(list.Update());
	ui.Layout({ 400, 400 });
	SWIM_CHECK(!list.Update()); // Stable.
	const auto bound = list.GetBoundRowCount();
	SWIM_CHECK(bound >= 10u && bound <= 13u); // ~200 / 20 rows plus overscan, never 10000.
	const auto row0 = list.FindRow(0);
	SWIM_REQUIRE(row0);
	SWIM_CHECK(ui.GetText(row0) == "Row 0");
	SWIM_CHECK_NEAR(ui.GetBounds(row0).Height, 20.0f, 1e-3f);
	SWIM_CHECK(!list.FindRow(500));

	// Scrolling far rebinds the pool at the new offset (no new rows).
	ui.SetScroll(list.GetViewport(), { 0.0f, 100000.0f });
	ui.Layout({ 400, 400 });
	SWIM_CHECK(list.Update());
	ui.Layout({ 400, 400 });
	SWIM_CHECK(list.GetBoundRowCount() <= bound + 1u); // Overscan above as well now; the pool is reused.
	const auto row5000 = list.FindRow(5000);
	SWIM_REQUIRE(row5000);
	SWIM_CHECK(ui.GetText(row5000) == "Row 5000");
	const auto viewport = ui.GetBounds(list.GetViewport());
	SWIM_CHECK_NEAR(ui.GetBounds(row5000).Y, viewport.Y, 1e-2f);

	// Click selects by index; keys move through unbound items using the item extent.
	Click(ui, Center(ui.GetBounds(row5000)));
	SWIM_CHECK(ui.GetFocus() == list.GetRoot());
	SWIM_CHECK_NEAR(ui.GetValue(list.GetRoot()), 5000.0f, 0.0f);
	SWIM_CHECK(ui.KeyDown(UiKey::End));
	SWIM_CHECK_NEAR(ui.GetValue(list.GetRoot()), 9999.0f, 0.0f);
	ui.Layout({ 400, 400 });
	list.Update();
	ui.Layout({ 400, 400 });
	const auto lastRow = list.FindRow(9999);
	SWIM_REQUIRE(lastRow);
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(lastRow), UiState::Checked | UiState::Focused));
	SWIM_CHECK_NEAR(ui.GetBounds(lastRow).Y + 20.0f, viewport.Y + viewport.Height, 1e-2f);

	// Shrinking the list clamps the selection and rebinds.
	list.SetItemCount(50);
	SWIM_CHECK_NEAR(ui.GetValue(list.GetRoot()), 49.0f, 0.0f);
	ui.Layout({ 400, 400 });
	list.Update();
	ui.Layout({ 400, 400 });
	SWIM_CHECK(list.FindRow(49));
	SWIM_CHECK(ui.GetText(list.FindRow(49)) == "Row 49");

	UiVirtualListDesc tooLong;
	tooLong.ItemCount = 1000000;
	tooLong.ItemHeight = 20.0f;
	SWIM_CHECK_THROWS(UiVirtualList(ui, ui.GetRoot(), tooLong), std::invalid_argument);
}

SWIM_TEST("UI.Selection", "EditableSliderValuesApplyTypedNumbersOnEnterAndBlur")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	UiSliderDesc desc;
	desc.Min = 0.0f;
	desc.Max = 10.0f;
	desc.Value = 2.0f;
	desc.Step = 0.5f;
	desc.ShowValue = true;
	desc.Decimals = 1;
	desc.EditableValue = true;
	const auto slider = CreateSlider(ui, ui.GetRoot(), desc);
	const auto other = CreateButton(ui, ui.GetRoot(), "Other");
	const auto label = ui.GetControl(slider).Parts.Label;
	SWIM_REQUIRE(label && ui.IsEditable(label));
	SWIM_CHECK(ui.GetText(label) == "2.0");
	ui.Layout({ 500, 200 });

	ui.Focus(label);
	ui.SetSelection(label, { 0, 3 });
	ui.TextInput("7.26");
	ui.SetValue(slider, 3.0f); // Code changes do not overwrite what is being typed.
	SWIM_CHECK(ui.GetText(label) == "7.26");
	ui.DrainEvents();
	SWIM_CHECK(ui.KeyDown(UiKey::Enter));
	auto events = ui.DrainEvents();
	SWIM_CHECK_NEAR(ui.GetValue(slider), 7.5f, 1e-6f); // Snapped to the step.
	SWIM_CHECK(ui.GetText(label) == "7.5");
	SWIM_CHECK_EQUAL(Of(events, UiEventKind::ValueCommitted, slider).size(), 1u);

	// Out of range clamps; applied when focus leaves.
	ui.SetSelection(label, { 0, 3 });
	ui.TextInput(" +42 ");
	ui.Focus(other);
	SWIM_CHECK_NEAR(ui.GetValue(slider), 10.0f, 1e-6f);
	SWIM_CHECK(ui.GetText(label) == "10.0");

	// Garbage restores the value.
	ui.Focus(label);
	ui.SetSelection(label, { 0, 4 });
	ui.TextInput("abc");
	ui.Focus({});
	SWIM_CHECK_NEAR(ui.GetValue(slider), 10.0f, 1e-6f);
	SWIM_CHECK(ui.GetText(label) == "10.0");
}
