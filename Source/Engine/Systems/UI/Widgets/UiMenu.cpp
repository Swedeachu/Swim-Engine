#include "Engine/Systems/UI/Widgets/UiMenu.h"

#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/Widgets/UiScrollBar.h"

namespace Swim::UI
{

	UiPopupList CreatePopupList(UiDocument& document)
	{
		// Hidden, placed by the document, blocking presses on what lies below.
		UiStyle rootStyle;
		rootStyle.Visible = false;
		rootStyle.Absolute = true;
		rootStyle.HitTest = true;
		const auto area = CreateScrollArea(document, document.GetRoot(), rootStyle, true, false, UiScrollBarVisibility::Overlay, false);
		auto items = document.GetStyle(area.Viewport);
		items.AlignItems = UiAlign::Stretch;
		document.SetStyle(area.Viewport, items);
		document.SetThemeClass(area.Root, UiThemeClass::Popup);
		return { area.Root, area.Viewport, area.Vertical };
	}

	UiPopupList CreateMenu(UiDocument& document)
	{
		return CreatePopupList(document);
	}

	UiNodeId AddMenuItem(UiDocument& document, const UiPopupList& menu, std::string label)
	{
		UiStyle style;
		style.TextWrap = Text::TextWrap::None;
		const auto item = Internal::CreateStyled(document, menu.Items, style);
		document.SetText(item, Internal::ThemeFonts(document), std::move(label), Internal::ThemeTextSize(document, UiThemeClass::MenuItem));
		UiControl control;
		control.Kind = UiControlKind::Button;
		document.SetControl(item, control);
		document.SetThemeClass(item, UiThemeClass::MenuItem);
		return item;
	}

	UiNodeId AddMenuSeparator(UiDocument& document, const UiPopupList& menu)
	{
		UiStyle style;
		const float gap = Internal::ThemeOf(document).Metrics.PopupPadding;
		style.Margin = { 0.0f, gap, 0.0f, gap };
		const auto separator = Internal::CreateStyled(document, menu.Items, style);
		document.SetThemeClass(separator, UiThemeClass::MenuSeparator);
		return separator;
	}

	void OpenMenu(UiDocument& document, const UiPopupList& menu, UiNodeId anchor, UiPopupSide side)
	{
		UiPopupDesc desc;
		desc.Anchor = anchor;
		desc.Side = side;
		desc.CloseOnActivate = true;
		document.OpenPopup(menu.Root, desc);
	}

} // namespace Swim::UI
