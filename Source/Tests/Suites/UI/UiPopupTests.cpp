#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
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

	std::size_t Count(const std::vector<UiEvent>& events, UiEventKind kind, UiNodeId node)
	{
		return std::count_if(events.begin(), events.end(),
			[&](const UiEvent& event)
			{
				return event.Kind == kind && event.Node == node;
			});
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

	bool Inside(const UiRect& inner, const UiRect& outer)
	{
		return inner.X >= outer.X - 1e-3f && inner.Y >= outer.Y - 1e-3f && inner.X + inner.Width <= outer.X + outer.Width + 1e-3f &&
			inner.Y + inner.Height <= outer.Y + outer.Height + 1e-3f;
	}

	// Index of the first paint quad of a node (paint order).
	std::ptrdiff_t FirstQuad(const std::vector<UiPaintQuad>& paint, UiNodeId node)
	{
		const auto it = std::find_if(paint.begin(), paint.end(),
			[&](const UiPaintQuad& quad)
			{
				return quad.Node == node;
			});
		return it == paint.end() ? -1 : it - paint.begin();
	}
} // namespace

SWIM_TEST("UI.Popups", "PopupsArePlacedFlippedAndKeptInsideTheCanvasAndPaintOnTop")
{
	UiDocument ui;
	const auto page = ui.Create(ui.GetRoot());
	auto pageStyle = Box(400, 300);
	pageStyle.Background = { 0.1f, 0.1f, 0.1f, 1.0f };
	ui.SetStyle(page, pageStyle);
	const auto anchor = ui.Create(page);
	auto anchorStyle = Box(80, 20);
	anchorStyle.Absolute = true;
	anchorStyle.Offset = { 100, 10 };
	anchorStyle.HitTest = true;
	ui.SetStyle(anchor, anchorStyle);
	const auto popup = ui.Create(ui.GetRoot());
	auto popupStyle = Box(120, 60);
	popupStyle.Background = { 1, 1, 1, 1 };
	popupStyle.HitTest = true;
	ui.SetStyle(popup, popupStyle);
	SWIM_CHECK_THROWS(ui.OpenPopup(anchor), std::invalid_argument); // Not a root child.
	UiPopupDesc bad;
	bad.Point = { std::nanf(""), 0 };
	SWIM_CHECK_THROWS(ui.OpenPopup(popup, bad), std::invalid_argument);

	UiPopupDesc desc;
	desc.Anchor = anchor;
	desc.Offset = { 0, 2 };
	ui.OpenPopup(popup, desc);
	ui.Layout({ 400, 300 });
	auto rect = ui.GetBounds(popup);
	SWIM_CHECK_NEAR(rect.X, 100.0f, 1e-3f);
	SWIM_CHECK_NEAR(rect.Y, 32.0f, 1e-3f); // Below the anchor plus the gap.
	SWIM_CHECK_NEAR(rect.Width, 120.0f, 1e-3f);
	// Painted after the page, which was created first.
	Swim::Text::GlyphAtlas paintAtlas;
	const auto& paint = ui.Paint(paintAtlas);
	SWIM_CHECK(FirstQuad(paint, popup) > FirstQuad(paint, page));
	SWIM_CHECK(ui.HitTest(Center(rect)) == popup); // On top for input too.

	// No room below: flips above. No room either way: clamped inside.
	anchorStyle.Offset = { 100, 260 };
	ui.SetStyle(anchor, anchorStyle);
	ui.Layout({ 400, 300 });
	rect = ui.GetBounds(popup);
	SWIM_CHECK_NEAR(rect.Y, 260.0f - 2.0f - 60.0f, 1e-3f);
	anchorStyle.Offset = { 370, 260 };
	ui.SetStyle(anchor, anchorStyle);
	ui.Layout({ 400, 300 });
	SWIM_CHECK(Inside(ui.GetBounds(popup), { 0, 0, 400, 300 }));
	SWIM_CHECK_NEAR(ui.GetBounds(popup).X, 280.0f, 1e-3f);

	// Right side flips left; AtPoint flips up/left; Center centers.
	desc.Side = UiPopupSide::Right;
	desc.Offset = {};
	ui.OpenPopup(popup, desc);
	ui.Layout({ 400, 300 });
	SWIM_CHECK_NEAR(ui.GetBounds(popup).X, 370.0f - 120.0f, 1e-3f);
	UiPopupDesc point;
	point.Side = UiPopupSide::AtPoint;
	point.Point = { 390, 290 };
	ui.OpenPopup(popup, point);
	ui.Layout({ 400, 300 });
	rect = ui.GetBounds(popup);
	SWIM_CHECK_NEAR(rect.X, 270.0f, 1e-3f);
	SWIM_CHECK_NEAR(rect.Y, 230.0f, 1e-3f);
	UiPopupDesc center;
	center.Side = UiPopupSide::Center;
	ui.OpenPopup(popup, center);
	ui.Layout({ 400, 300 });
	rect = ui.GetBounds(popup);
	SWIM_CHECK_NEAR(rect.X, 140.0f, 1e-3f);
	SWIM_CHECK_NEAR(rect.Y, 120.0f, 1e-3f);
	// At DPI 2 placement happens in logical units.
	ui.Layout({ 800, 600 }, 2.0f);
	rect = ui.GetBounds(popup);
	SWIM_CHECK_NEAR(rect.X, 140.0f, 1e-3f);

	SWIM_CHECK(ui.ClosePopup(popup));
	SWIM_CHECK(!ui.ClosePopup(popup));
	SWIM_CHECK(!ui.IsPopupOpen(popup));
	ui.Layout({ 400, 300 });
	SWIM_CHECK(!ui.HitTest({ 200, 150 }) || ui.HitTest({ 200, 150 }) != popup);
	const auto events = ui.DrainEvents();
	SWIM_CHECK(Count(events, UiEventKind::PopupOpened, popup) >= 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::PopupClosed, popup), 1u);

	// Removing an open popup drops it from the stack.
	ui.OpenPopup(popup, center);
	ui.Remove(popup);
	SWIM_CHECK(!ui.GetTopPopup());
	ui.Layout({ 400, 300 });
}

SWIM_TEST("UI.Popups", "MenusTakeFocusNavigateActivateAndCloseInStackOrder")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto file = CreateButton(ui, ui.GetRoot(), "File");
	const auto menu = CreateMenu(ui);
	const auto open = AddMenuItem(ui, menu, "Open");
	AddMenuSeparator(ui, menu);
	const auto recent = AddMenuItem(ui, menu, "Recent");
	const auto quit = AddMenuItem(ui, menu, "Quit");
	const auto submenu = CreateMenu(ui);
	const auto first = AddMenuItem(ui, submenu, "a.scene");
	ui.Layout({ 600, 400 });
	ui.Focus(file);
	OpenMenu(ui, menu, file);
	ui.Layout({ 600, 400 });
	SWIM_CHECK(ui.GetFocus() == open); // FocusFirst after placement.
	const auto menuRect = ui.GetBounds(menu.Root);
	SWIM_CHECK_NEAR(menuRect.Y, ui.GetBounds(file).Y + ui.GetBounds(file).Height, 1e-3f);
	// Items fill the menu's width.
	SWIM_CHECK_NEAR(ui.GetBounds(open).Width, ui.GetBounds(quit).Width, 1e-3f);

	// Arrows stay inside the menu (the separator is skipped: not focusable).
	SWIM_CHECK(ui.KeyDown(UiKey::Down));
	SWIM_CHECK(ui.GetFocus() == recent);
	SWIM_CHECK(ui.KeyDown(UiKey::Down));
	SWIM_CHECK(ui.GetFocus() == quit);
	SWIM_CHECK(!ui.KeyDown(UiKey::Down)); // No wrap, never out to the page.
	SWIM_CHECK(ui.GetFocus() == quit);

	// A submenu to the right; Escape closes only the top one and focus returns to its anchor.
	UiPopupDesc side;
	side.Anchor = recent;
	side.Side = UiPopupSide::Right;
	side.CloseOnActivate = true;
	ui.OpenPopup(submenu.Root, side);
	ui.Layout({ 600, 400 });
	SWIM_CHECK(ui.GetFocus() == first);
	SWIM_CHECK_NEAR(ui.GetBounds(submenu.Root).X, ui.GetBounds(recent).X + ui.GetBounds(recent).Width, 1e-3f);
	SWIM_CHECK(ui.KeyDown(UiKey::Escape));
	SWIM_CHECK(!ui.IsPopupOpen(submenu.Root));
	SWIM_CHECK(ui.IsPopupOpen(menu.Root));
	SWIM_CHECK(ui.GetFocus() == recent);

	// Closing a lower popup closes those above it.
	ui.OpenPopup(submenu.Root, side);
	SWIM_CHECK(ui.ClosePopup(menu.Root));
	SWIM_CHECK(!ui.IsPopupOpen(submenu.Root));
	SWIM_CHECK(ui.GetFocus() == file);

	// Activation (Enter) clicks and closes; focus returns to the anchor.
	OpenMenu(ui, menu, file);
	ui.Layout({ 600, 400 });
	ui.DrainEvents();
	SWIM_CHECK(ui.KeyDown(UiKey::Enter));
	auto events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Click, open), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::PopupClosed, menu.Root), 1u);
	SWIM_CHECK(ui.GetFocus() == file);

	// A pointer click on an item closes; a click on the menu's background does not.
	OpenMenu(ui, menu, file);
	ui.Layout({ 600, 400 });
	const auto separatorPoint = Center(ui.GetBounds(open));
	Click(ui, { separatorPoint.X, ui.GetBounds(open).Y + ui.GetBounds(open).Height + 1.0f }); // Separator gap.
	SWIM_CHECK(ui.IsPopupOpen(menu.Root));
	Click(ui, Center(ui.GetBounds(quit)));
	events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Click, quit), 1u);
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root));

	// A press outside dismisses; the anchor press does not dismiss (it toggles in dropdowns).
	OpenMenu(ui, menu, file);
	ui.Layout({ 600, 400 });
	ui.PointerDown(Center(ui.GetBounds(file)));
	ui.PointerUp(Center(ui.GetBounds(file)));
	SWIM_CHECK(ui.IsPopupOpen(menu.Root));
	Click(ui, { 590, 390 });
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root));
	ui.OpenPopup(menu.Root);
	ui.DismissPopups();
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root));
	UiPopupDesc sticky;
	sticky.LightDismiss = false;
	ui.OpenPopup(menu.Root, sticky);
	ui.Layout({ 600, 400 });
	Click(ui, { 590, 390 });
	SWIM_CHECK(ui.IsPopupOpen(menu.Root));
	ui.CloseAllPopups();
	SWIM_CHECK(!ui.GetTopPopup());

	// Opening an unrelated popup closes open light-dismiss ones; a submenu keeps its parent.
	OpenMenu(ui, menu, file);
	OpenMenu(ui, submenu, file);
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root));
	SWIM_CHECK(ui.IsPopupOpen(submenu.Root));
	ui.CloseAllPopups();
	OpenMenu(ui, menu, file);
	ui.Layout({ 600, 400 });
	OpenMenu(ui, submenu, recent, UiPopupSide::Right);
	SWIM_CHECK(ui.IsPopupOpen(menu.Root) && ui.IsPopupOpen(submenu.Root));
	ui.CloseAllPopups();
}

SWIM_TEST("UI.Popups", "ContextMenusOpenAtThePointerOrBelowTheFocusedNode")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto panel = ui.Create(ui.GetRoot());
	auto style = Box(300, 200);
	style.HitTest = true;
	ui.SetStyle(panel, style);
	const auto button = CreateButton(ui, panel, "Target");
	const auto menu = CreateMenu(ui);
	const auto copy = AddMenuItem(ui, menu, "Copy");
	SWIM_CHECK_THROWS(ui.SetContextMenu(panel, button), std::invalid_argument);
	ui.SetContextMenu(panel, menu.Root);
	ui.Layout({ 500, 400 });

	// Registered on an ancestor: the button's context menu is the panel's.
	const auto at = Center(ui.GetBounds(button));
	SWIM_CHECK(ui.OpenContextMenu(at));
	auto events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::ContextMenu, panel), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::PopupOpened, menu.Root), 1u);
	ui.Layout({ 500, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(menu.Root).X, at.X, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(menu.Root).Y, at.Y, 1e-3f);
	SWIM_CHECK(ui.GetFocus() == copy);
	ui.KeyDown(UiKey::Enter);
	SWIM_CHECK(!ui.IsPopupOpen(menu.Root)); // Context menus close on activation.

	// Outside any registration: nothing.
	SWIM_CHECK(!ui.OpenContextMenu({ 450, 350 }));

	// Keyboard: below the focused node.
	ui.Focus(button);
	SWIM_CHECK(ui.OpenContextMenuForFocus());
	ui.Layout({ 500, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(menu.Root).Y, ui.GetBounds(button).Y + ui.GetBounds(button).Height, 1e-3f);
	ui.KeyDown(UiKey::Escape);
	SWIM_CHECK(ui.GetFocus() == button);
	ui.SetContextMenu(panel, {});
	SWIM_CHECK(!ui.OpenContextMenuForFocus());
}

SWIM_TEST("UI.Popups", "TooltipsOpenAfterTheDelayAndCloseOnLeavePressAndEscape")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto button = CreateButton(ui, ui.GetRoot(), "Save");
	const auto label = CreateLabel(ui, ui.GetRoot(), "Not interactive");
	const auto tip = CreateTooltip(ui, button, "Saves the scene", 0.5f);
	const auto labelTip = CreateTooltip(ui, label, "Labels too", 0.0f);
	SWIM_CHECK_THROWS(ui.SetTooltip(button, tip, -1.0f), std::invalid_argument);
	ui.Layout({ 500, 300 });
	const auto over = Center(ui.GetBounds(button));
	ui.PointerMove(over);
	SWIM_CHECK(ui.Update(0.3f)); // Still waiting.
	SWIM_CHECK(!ui.IsPopupOpen(tip));
	ui.Update(0.3f);
	SWIM_CHECK(ui.IsPopupOpen(tip));
	ui.Layout({ 500, 300 });
	const auto rect = ui.GetBounds(tip);
	SWIM_CHECK_NEAR(rect.X, over.X, 1e-3f);
	SWIM_CHECK_NEAR(rect.Y, over.Y + 20.0f, 1e-3f);
	SWIM_CHECK(ui.HitTest(Center(rect)) != tip); // Tooltips never take input.
	SWIM_CHECK(ui.GetFocus() != tip);

	// Pressing hides it and suppresses it until the pointer moves to another target.
	ui.PointerDown(over);
	ui.PointerUp(over);
	SWIM_CHECK(!ui.IsPopupOpen(tip));
	ui.Layout({ 500, 300 });
	ui.Update(1.0f);
	SWIM_CHECK(!ui.IsPopupOpen(tip));

	// Another target with no delay; leaving closes it.
	ui.PointerMove(Center(ui.GetBounds(label)));
	ui.Update(0.0f);
	SWIM_CHECK(ui.IsPopupOpen(labelTip));
	ui.Layout({ 500, 300 });
	ui.PointerMove({ 490, 290 });
	ui.Update(0.0f);
	SWIM_CHECK(!ui.IsPopupOpen(labelTip));

	// Back on the button: shows again; Escape hides it.
	ui.Layout({ 500, 300 });
	ui.PointerMove(over);
	ui.Update(0.6f);
	SWIM_CHECK(ui.IsPopupOpen(tip));
	ui.Focus(button);
	SWIM_CHECK(ui.KeyDown(UiKey::Escape));
	SWIM_CHECK(!ui.IsPopupOpen(tip));
	SWIM_CHECK(ui.GetFocus() == button); // Escape closed the tooltip, not the focus.

	// Unregistering removes the behaviour.
	ui.SetTooltip(button, {});
	ui.Layout({ 500, 300 });
	ui.PointerMove({ 490, 290 });
	ui.Update(0.0f);
	ui.PointerMove(over);
	ui.Update(5.0f);
	SWIM_CHECK(!ui.IsPopupOpen(tip));
}

SWIM_TEST("UI.Popups", "ModalDialogsBlockThePageTrapTabAndRestoreFocus")
{
	UiDocument ui;
	ui.SetTheme(FontTheme());
	const auto page = CreateButton(ui, ui.GetRoot(), "Delete");
	const auto pageField = CreateTextField(ui, ui.GetRoot());
	(void)pageField;
	auto modal = CreateModal(ui, "Delete the scene?");
	CreateLabel(ui, modal.Content, "This cannot be undone.");
	const auto cancel = AddModalButton(ui, modal, "Cancel");
	const auto confirm = AddModalButton(ui, modal, "Delete");
	ui.Layout({ 800, 600 });
	ui.Focus(page);
	OpenModal(ui, modal);
	ui.Layout({ 800, 600 });
	SWIM_CHECK(ui.GetFocus() == cancel);
	// The scrim covers the canvas; the dialog is centered.
	const auto scrim = ui.GetBounds(modal.Root);
	SWIM_CHECK_NEAR(scrim.Width, 800.0f, 1e-3f);
	SWIM_CHECK_NEAR(scrim.Height, 600.0f, 1e-3f);
	const auto dialog = ui.GetBounds(modal.Dialog);
	SWIM_CHECK_NEAR(dialog.X + dialog.Width * 0.5f, 400.0f, 0.5f);
	SWIM_CHECK_NEAR(dialog.Y + dialog.Height * 0.5f, 300.0f, 0.5f);
	SWIM_CHECK(dialog.Width >= ui.GetTheme()->Metrics.DialogMinWidth - 1e-3f);

	// The page does not take input: hits land on the scrim, Tab stays inside.
	const auto pageCenter = Center(ui.GetBounds(page));
	SWIM_CHECK(ui.HitTest(pageCenter) == modal.Root);
	ui.DrainEvents();
	Click(ui, pageCenter);
	auto events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Click, page), 0u);
	SWIM_CHECK(ui.IsPopupOpen(modal.Root)); // No light dismiss.
	ui.Focus(cancel);
	ui.KeyDown(UiKey::Tab);
	SWIM_CHECK(ui.GetFocus() == confirm);
	ui.KeyDown(UiKey::Tab);
	SWIM_CHECK(ui.GetFocus() == cancel);
	SWIM_CHECK(!ui.Wheel(pageCenter, { 0, 10 }));

	// Escape closes; focus returns to where it was.
	SWIM_CHECK(ui.KeyDown(UiKey::Escape));
	SWIM_CHECK(!ui.IsPopupOpen(modal.Root));
	SWIM_CHECK(ui.GetFocus() == page);
	ui.Layout({ 800, 600 });
	SWIM_CHECK(ui.HitTest(pageCenter) == page);

	// Buttons do not close it on their own (no CloseOnActivate): the application decides.
	OpenModal(ui, modal);
	ui.Layout({ 800, 600 });
	Click(ui, Center(ui.GetBounds(confirm)));
	events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Click, confirm), 1u);
	SWIM_CHECK(ui.IsPopupOpen(modal.Root));
	ui.ClosePopup(modal.Root);
	SWIM_CHECK(ui.GetFocus() == page);
}
