#pragma once

#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"

#include <functional>
#include <string>
#include <vector>

// Widget layer over UiDocument nodes (critical-path item 79). Widgets are ordinary nodes
// (a control and its part nodes) styled by the document's theme: fonts, sizes and colors
// come from UiDocument::GetTheme(), and SetTheme restyles every widget. Every part stays
// reachable (UiDocument::GetControl(node).Parts) for per-node rules, images or
// replacement. Behaviour lives in the document, so widgets work on any canvas.
namespace Swim::UI
{
	// Themed helpers. Those that show text throw std::logic_error when the document's
	// theme has no fonts.
	UiNodeId CreatePanel(UiDocument& document, UiNodeId parent, UiFlow flow = UiFlow::Column);
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::string text);
	// A Button control: Click on release inside or Enter/Space/gamepad A while focused.
	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::string label);
	// An editable, clipped text node.
	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, const UiTextEditOptions& options = {});
	// Row [box [mark, mixed mark], label]; an empty label creates no label part.
	UiNodeId CreateCheckbox(UiDocument& document, UiNodeId parent, std::string label, UiCheckState state = UiCheckState::Unchecked);
	// Row [track [knob], label].
	UiNodeId CreateToggle(UiDocument& document, UiNodeId parent, std::string label, bool on = false);

	struct UiSliderDesc
	{
		float Min = 0.0f;
		float Max = 1.0f;
		float Value = 0.0f;
		float Step = 0.0f;
		float PageStep = 0.0f;
		UiOrientation Orientation = UiOrientation::Horizontal;
		UiTrackClick TrackClick = UiTrackClick::Jump;
		std::uint32_t Ticks = 0; // >= 2: evenly spaced tick marks from Min to Max (inclusive).
		bool ShowValue = false;	 // A value label next to the slider (the pair sits in a row).
		std::int32_t Decimals = 0;
		// With ShowValue: the label is a text field; Enter or leaving it applies the typed
		// number (clamped and snapped), anything unparsable restores the value.
		bool EditableValue = false;
	};

	// [track, ticks..., fill, thumb], all placed by the slider; with ShowValue the slider
	// and its label (Parts.Label, updated by the document) share a new row container.
	UiNodeId CreateSlider(UiDocument& document, UiNodeId parent, const UiSliderDesc& desc = {});

	struct UiScrollBarDesc
	{
		UiOrientation Orientation = UiOrientation::Vertical;
		UiScrollBarVisibility Visibility = UiScrollBarVisibility::Auto;
		UiTrackClick TrackClick = UiTrackClick::Page;
		bool StepButtons = false; // Decrement/increment buttons at the ends (held: repeat).
	};

	// A bar [thumb, step buttons] driving target's scroll offset; target must be a clipped node.
	UiNodeId CreateScrollBar(UiDocument& document, UiNodeId parent, UiNodeId target, const UiScrollBarDesc& desc = {});

	struct UiScrollArea
	{
		UiNodeId Root;
		UiNodeId Viewport; // The clipped content node: add children here.
		UiNodeId Vertical; // Empty when not requested.
		UiNodeId Horizontal;
	};

	// A root (styled by rootStyle, typically a size) holding a clipped viewport and its
	// scroll bars: in flow next to the viewport, or floating over its edges for Overlay.
	UiScrollArea CreateScrollArea(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, bool vertical = true,
		bool horizontal = false, UiScrollBarVisibility visibility = UiScrollBarVisibility::Auto, bool stepButtons = false);

	// --- Selection controls. ---
	// A RadioGroup of option rows [circle [dot], label] (the group's children); Value is the
	// selected index (-1: none). Horizontal groups lay their options out in a row.
	UiNodeId CreateRadioGroup(UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected = -1,
		UiOrientation orientation = UiOrientation::Vertical);
	// Appends an option (index = the current count) and returns its row.
	UiNodeId AddRadioOption(UiDocument& document, UiNodeId group, std::string label);

	struct UiListView
	{
		UiNodeId Root;	   // The ListView control (focusable, themed ListView).
		UiNodeId Viewport; // Clipped: the option rows (and the ScrollTarget).
		UiNodeId ScrollBar;
	};

	// A scrolling list of single-line text rows (themed MenuItem options).
	UiListView CreateListView(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, const std::vector<std::string>& items = {},
		std::int32_t selected = -1);
	UiNodeId AddListItem(UiDocument& document, const UiListView& list, std::string text);

	// A popup panel (a hidden child of the root) whose Items scroll past UiMetrics::PopupMaxHeight.
	struct UiPopupList
	{
		UiNodeId Root; // The popup: pass to OpenPopup/SetContextMenu.
		UiNodeId Items;
		UiNodeId ScrollBar;
	};

	struct UiDropdown
	{
		UiNodeId Root;	// The Dropdown control: a row [label, arrow].
		UiNodeId Label; // Shows the selected option's text (or the placeholder).
		UiNodeId Arrow;
		UiPopupList List; // Opened below Root by activation; options are its Items' children.
	};

	UiDropdown CreateDropdown(UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected = -1,
		std::string placeholder = {});
	UiNodeId AddDropdownOption(UiDocument& document, const UiDropdown& dropdown, std::string text);

	// --- Menus, tooltips and modal dialogs (UiDocument popups). ---
	UiPopupList CreateMenu(UiDocument& document);
	// A Button entry: its Click is the command; the menu closes when opened as a menu.
	UiNodeId AddMenuItem(UiDocument& document, const UiPopupList& menu, std::string label);
	UiNodeId AddMenuSeparator(UiDocument& document, const UiPopupList& menu);
	// Opens a menu beside an anchor (focus moves into it; activation or Escape closes it).
	void OpenMenu(UiDocument& document, const UiPopupList& menu, UiNodeId anchor, UiPopupSide side = UiPopupSide::Below);

	// A themed tooltip (a hidden child of the root) registered on target.
	UiNodeId CreateTooltip(UiDocument& document, UiNodeId target, std::string text, float delaySeconds = 0.5f);

	struct UiModal
	{
		UiNodeId Root;	  // The full-canvas scrim (the popup; blocks the page below).
		UiNodeId Dialog;  // The centered panel [title, content, buttons].
		UiNodeId Title;	  // Empty without a title.
		UiNodeId Content; // Add the body here.
		UiNodeId Buttons; // A right-aligned row: AddModalButton.
	};

	UiModal CreateModal(UiDocument& document, std::string title);
	UiNodeId AddModalButton(UiDocument& document, const UiModal& modal, std::string label);
	// Opens it modal: only the dialog takes input, Tab stays inside, focus goes to its first
	// focusable node and returns on close. Presses on the scrim do not close it; Escape does.
	void OpenModal(UiDocument& document, const UiModal& modal);

	// --- Virtualized lists. ---
	struct UiVirtualListDesc
	{
		std::uint32_t ItemCount = 0;
		float ItemHeight = 0.0f; // 0: the theme's ControlHeight. ItemCount x ItemHeight <= 1,000,000.
		// Fills a row for an index (text, images, child nodes); rows are reused.
		std::function<void(UiDocument&, UiNodeId row, std::uint32_t index)> Bind;
		UiStyle Style;				// The list's root (typically a size).
		std::uint32_t Overscan = 2; // Extra rows bound above and below the viewport.
		std::int32_t Selected = -1;
	};

	// A ListView whose rows exist only while visible: a pool of MenuItem rows placed at
	// index x ItemHeight inside a content node ItemCount x ItemHeight long. Selection, keys
	// and scrolling behave as for CreateListView (UiControl::ItemCount/ItemExtent).
	class UiVirtualList
	{
	  public:
		UiVirtualList(UiDocument& document, UiNodeId parent, UiVirtualListDesc desc);

		// Binds the rows the viewport shows (with the scroll offset and viewport of the last
		// Layout). Call after Layout; true when rows changed (Layout again before Paint).
		bool Update();
		void SetItemCount(std::uint32_t count); // Clamps the selection; rebinds every row.
		void Refresh();							// Rebinds every row (the items changed).

		UiNodeId GetRoot() const { return list.Root; }

		UiNodeId GetViewport() const { return list.Viewport; }

		std::uint32_t GetItemCount() const { return desc.ItemCount; }

		std::uint32_t GetBoundRowCount() const;		 // Rows showing an item.
		UiNodeId FindRow(std::uint32_t index) const; // The row showing index (empty when not bound).

		std::uint64_t GetBindCount() const { return binds; }

	  private:
		UiDocument& document;
		UiVirtualListDesc desc;
		UiListView list;
		UiNodeId content;

		struct Row
		{
			UiNodeId Node;
			std::int64_t Index = -1;
		};

		std::vector<Row> rows;
		std::uint64_t binds = 0;
		float viewport = 0.0f;
	};

	// Unthemed helpers with explicit fonts and styles.
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style = {});
	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style = {});
	// A clipped node: children scroll with UiDocument::Wheel/SetScroll.
	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style);
	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options = {}, const UiStyle& style = {});
} // namespace Swim::UI
