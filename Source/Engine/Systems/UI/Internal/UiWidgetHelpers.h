#pragma once

// Helpers shared by the engine's widget units (Widgets/Ui*.cpp). Not part of the public API.

#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace Swim::UI::Internal
{

	inline const UiTheme& ThemeOf(const UiDocument& document)
	{
		return *document.GetTheme();
	}

	// The theme's fonts (themed text throws without them).
	inline std::shared_ptr<const Text::FontCollection> ThemeFonts(const UiDocument& document)
	{
		const auto& fonts = ThemeOf(document).Fonts;

		if (!fonts)
		{
			throw std::logic_error("Themed UI text needs UiTheme::Fonts");
		}

		return fonts;
	}

	inline float ThemeTextSize(const UiDocument& document, UiThemeClass themeClass)
	{
		return ThemeOf(document).Class(themeClass).TextSize;
	}

	inline UiNodeId CreateStyled(UiDocument& document, UiNodeId parent, const UiStyle& style)
	{
		const auto node = document.Create(parent);
		document.SetStyle(node, style);
		return node;
	}

	inline UiNodeId ThemedLabel(UiDocument& document, UiNodeId parent, std::string text)
	{
		const auto node = document.Create(parent);
		document.SetText(node, ThemeFonts(document), std::move(text), ThemeTextSize(document, UiThemeClass::Label));
		document.SetThemeClass(node, UiThemeClass::Label);
		return node;
	}

	// Axis helpers of horizontal/vertical controls.
	inline bool Horizontal(const UiControl& control)
	{
		return control.Orientation == UiOrientation::Horizontal;
	}

	inline float Along(const UiRect& rect, bool horizontal)
	{
		return horizontal ? rect.X : rect.Y;
	}

	inline float Length(const UiRect& rect, bool horizontal)
	{
		return horizontal ? rect.Width : rect.Height;
	}

	inline float Length(UiPoint size, bool horizontal)
	{
		return horizontal ? size.X : size.Y;
	}

	inline float Axis(const UiControl& control, UiPoint point)
	{
		return Horizontal(control) ? point.X : point.Y;
	}

} // namespace Swim::UI::Internal
