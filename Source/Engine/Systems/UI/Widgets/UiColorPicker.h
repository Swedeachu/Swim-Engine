#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <functional>

namespace Swim::UI
{

	struct UiColorPickerDesc
	{
		UiColor Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // Straight alpha, linear.
		bool Alpha = true;						 // Show the opacity strip and accept #RRGGBBAA.
		float Size = 168.0f;					 // Edge of the saturation/value square, logical units.
	};

	// Colours as the picker reports them: straight alpha, linear RGB.
	using UiColorCallback = std::function<void(const UiColor& color)>;

	// A colour picker panel: a saturation/value square, a hue strip, an opacity strip, a
	// preview swatch and a hex field (#RRGGBB or #RRGGBBAA, sRGB). Every pick and drag step
	// calls onChange; releasing a drag or entering a hex value also calls onCommit. Needs a
	// theme with fonts (the hex field). Returns the panel's node.
	UiNodeId CreateColorPicker(
		UiDocument& document, UiNodeId parent, const UiColorPickerDesc& desc, UiColorCallback onChange, UiColorCallback onCommit = {});

	// The picker in a popup next to anchor (below it, light-dismissed); the popup removes
	// itself when it closes, committing the colour reached. Returns the popup's node.
	UiNodeId OpenColorPicker(
		UiDocument& document, UiNodeId anchor, const UiColorPickerDesc& desc, UiColorCallback onChange, UiColorCallback onCommit = {});

} // namespace Swim::UI
