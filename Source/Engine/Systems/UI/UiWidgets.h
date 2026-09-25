#pragma once

#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"

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

	// Unthemed helpers with explicit fonts and styles.
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style = {});
	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style = {});
	// A clipped node: children scroll with UiDocument::Wheel/SetScroll.
	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style);
	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options = {}, const UiStyle& style = {});
} // namespace Swim::UI
