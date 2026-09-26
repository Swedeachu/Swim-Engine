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
		// Sleek dark mode: blue-black panels, lifted slate surfaces and an electric-blue accent.
		UiColor Panel{ 0.043f, 0.051f, 0.071f, 0.92f };
		UiColor Surface{ 0.09f, 0.105f, 0.14f, 1.0f };
		UiColor SurfaceHover{ 0.13f, 0.155f, 0.21f, 1.0f };
		UiColor SurfacePressed{ 0.06f, 0.07f, 0.1f, 1.0f };
		UiColor Field{ 0.03f, 0.035f, 0.05f, 1.0f };
		UiColor Border{ 0.16f, 0.19f, 0.26f, 1.0f };
		UiColor Accent{ 0.2f, 0.52f, 1.0f, 1.0f };
		UiColor AccentHover{ 0.35f, 0.63f, 1.0f, 1.0f };
		UiColor AccentPressed{ 0.13f, 0.4f, 0.85f, 1.0f };
		UiColor OnAccent{ 0.97f, 0.98f, 1.0f, 1.0f };
		UiColor Focus{ 0.38f, 0.66f, 1.0f, 1.0f };
		UiColor Text{ 0.9f, 0.93f, 0.98f, 1.0f };
		UiColor TextMuted{ 0.5f, 0.56f, 0.66f, 1.0f };
		UiColor Track{ 0.14f, 0.17f, 0.23f, 1.0f };
		UiColor Selection{ 0.2f, 0.52f, 1.0f, 0.4f };
		UiColor Caret{ 0.55f, 0.78f, 1.0f, 1.0f };
		UiColor Popup{ 0.055f, 0.065f, 0.09f, 0.98f }; // Menus, dropdown lists, dialogs.
		UiColor Tooltip{ 0.02f, 0.025f, 0.04f, 0.95f };
		UiColor Scrim{ 0.0f, 0.0f, 0.02f, 0.55f }; // Behind modal dialogs.
	};

	// Logical units; the classes of horizontal controls, swapped for vertical ones.
	struct UiMetrics
	{
		float CornerRadius = 7.0f;
		float BorderWidth = 1.0f;
		float FocusWidth = 2.0f;
		float Spacing = 8.0f;
		UiEdges ButtonPadding{ 12.0f, 6.0f, 12.0f, 6.0f };
		UiEdges FieldPadding{ 6.0f, 4.0f, 6.0f, 4.0f };
		float ControlHeight = 28.0f;
		float TextSize = 16.0f;
		float LabelTextSize = 16.0f;
		float SliderLength = 160.0f;
		float SliderTrack = 4.0f;
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
