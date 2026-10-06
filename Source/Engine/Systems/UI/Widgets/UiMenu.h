#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <string>

namespace Swim::UI
{

	// A popup panel (a hidden child of the root) whose Items scroll past UiMetrics::PopupMaxHeight.
	struct UiPopupList
	{
		UiNodeId Root; // The popup: pass to OpenPopup/SetContextMenu.
		UiNodeId Items;
		UiNodeId ScrollBar;
	};

	struct UiMenuDesc
	{
	};

	// A themed popup list (menus, dropdown lists).
	UiPopupList CreatePopupList(UiDocument& document);

	UiPopupList CreateMenu(UiDocument& document);

	// A Button entry: its Click is the command; the menu closes when opened as a menu.
	UiNodeId AddMenuItem(UiDocument& document, const UiPopupList& menu, std::string label);

	UiNodeId AddMenuSeparator(UiDocument& document, const UiPopupList& menu);

	// Opens a menu beside an anchor (focus moves into it; activation or Escape closes it).
	void OpenMenu(UiDocument& document, const UiPopupList& menu, UiNodeId anchor, UiPopupSide side = UiPopupSide::Below);

} // namespace Swim::UI
