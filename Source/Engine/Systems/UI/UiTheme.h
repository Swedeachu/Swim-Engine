#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

// Document-level themes (critical-path item 79). A theme turns a palette, metrics and a
// font chain into one UiClassStyle per UiThemeClass: a base style (paint, sizes) plus
// state rules (hover, pressed, focused, disabled, checked, ...). Themed nodes take the
// parts of their class chosen by UiThemeApply; per-node overrides are the node's own
// state rules (UiDocument::SetStateRules), applied after the class's.
namespace Swim::UI
{
	struct UiPalette
	{
		// Dark mode, authored in sRGB (UiSrgbHex): near-black blue panels, slate surfaces a
		// step lighter and a saturated electric-blue accent. (These used to be sRGB numbers
		// read as linear, which displayed every "black" as mid grey and washed the accent out.)
		UiColor Panel = UiSrgbHex(0x0a0d13, 0.96f);
		UiColor Surface = UiSrgbHex(0x161b25);
		UiColor SurfaceHover = UiSrgbHex(0x212938);
		UiColor SurfacePressed = UiSrgbHex(0x0f131b);
		UiColor Field = UiSrgbHex(0x06080c);
		UiColor Border = UiSrgbHex(0x2a3344);
		UiColor Accent = UiSrgbHex(0x1f7aff);
		UiColor AccentHover = UiSrgbHex(0x4a95ff);
		UiColor AccentPressed = UiSrgbHex(0x1560d6);
		UiColor OnAccent = UiSrgbHex(0xffffff);
		UiColor Focus = UiSrgbHex(0x5ea2ff);
		UiColor Text = UiSrgbHex(0xe9eef6);
		UiColor TextMuted = UiSrgbHex(0x8b96a8);
		UiColor Track = UiSrgbHex(0x252d3b);
		UiColor Selection = UiSrgbHex(0x1f7aff, 0.4f);
		UiColor Caret = UiSrgbHex(0x8cc2ff);
		UiColor Popup = UiSrgbHex(0x0d1118, 0.98f); // Menus, dropdown lists, dialogs.
		UiColor Tooltip = UiSrgbHex(0x05070b, 0.96f);
		UiColor Scrim = UiSrgbHex(0x000000, 0.6f); // Behind modal dialogs.
	};

	// Logical units; the classes of horizontal controls, swapped for vertical ones.
	struct UiMetrics
	{
		float CornerRadius = 7.0f;
		float BorderWidth = 1.0f;
		float FocusWidth = 2.5f;
		float Spacing = 8.0f;
		UiEdges ButtonPadding{ 12.0f, 6.0f, 12.0f, 6.0f };
		UiEdges FieldPadding{ 6.0f, 4.0f, 6.0f, 4.0f };
		float ControlHeight = 28.0f;
		float TextSize = 16.0f;
		float LabelTextSize = 16.0f;
		float SliderLength = 160.0f;
		float SliderTrack = 6.0f;
		float SliderThumb = 16.0f;
		float CheckboxSize = 18.0f;
		float ToggleWidth = 36.0f;
		float ToggleHeight = 20.0f;
		float ToggleInset = 2.0f;
		float ScrollBarThickness = 10.0f;
		float ScrollBarMinThumb = 24.0f;
		float RadioSize = 18.0f;
		UiEdges MenuItemPadding{ 10.0f, 5.0f, 10.0f, 5.0f };
		float PopupPadding = 4.0f;
		float PopupMaxHeight = 280.0f; // Longer menus and dropdown lists scroll.
		float DropdownArrowSize = 8.0f;
		UiEdges TooltipPadding{ 8.0f, 4.0f, 8.0f, 4.0f };
		float TooltipTextSize = 14.0f;
		float DialogMinWidth = 280.0f;
		float DialogTitleTextSize = 20.0f;
		// Eases state changes (and toggle knobs) when > 0; call UiDocument::Update every frame.
		float TransitionSeconds = 0.0f;
		float DisabledOpacity = 0.45f;
	};

	struct UiClassStyle
	{
		UiStyle Style;			// Paint fields and theme-owned layout fields (UiThemeApply).
		float TextSize = 16.0f; // UiThemeApply::Text.
		std::vector<UiStateRule> Rules;
	};

	class UiTheme
	{
	  public:
		UiPalette Palette;
		UiMetrics Metrics;
		// Fonts of themed text (labels, buttons, text fields); null leaves text unthemed and
		// makes the text-creating widget helpers throw.
		std::shared_ptr<const Text::FontCollection> Fonts;
		// Adjusts any generated class after the palette and metrics (colors, sizes, extra
		// rules, image skins). Keep it deterministic: it runs on every Build.
		std::function<void(UiThemeClass, UiClassStyle&)> Customize;

		// The class generated from the palette and metrics, then Customize.
		UiClassStyle Class(UiThemeClass themeClass) const;

		// Every class, as UiDocument::SetTheme caches them. Throws std::invalid_argument
		// when a class's style is invalid (UiDocument::SetStyle's validation).
		std::array<UiClassStyle, static_cast<std::size_t>(UiThemeClass::Count)> Build() const;
	};
} // namespace Swim::UI
