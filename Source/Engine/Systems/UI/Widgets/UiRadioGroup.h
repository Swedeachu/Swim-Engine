#pragma once

#include "Engine/Systems/UI/Widgets/UiSelection.h"

#include <string>
#include <vector>

namespace Swim::UI
{

	// One choice among option rows: a click selects; arrows select the previous/next option
	// (wrapping), Home/End the ends; Space/Enter select the first when nothing is.
	class UiRadioGroupControl final : public UiSelectionControl
	{

	  public:

		bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers modifiers) override;

	};

	struct UiRadioGroupDesc
	{
		std::vector<std::string> Options;
		std::int32_t Selected = -1;
		UiOrientation Orientation = UiOrientation::Vertical;
	};

	// A RadioGroup of option rows [circle [dot], label] (the group's children); Value is the
	// selected index (-1: none). Horizontal groups lay their options out in a row.
	UiNodeId CreateRadioGroup(UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected = -1,
		UiOrientation orientation = UiOrientation::Vertical);

	inline UiNodeId CreateRadioGroup(UiDocument& document, UiNodeId parent, const UiRadioGroupDesc& desc)
	{
		return CreateRadioGroup(document, parent, desc.Options, desc.Selected, desc.Orientation);
	}

	// Appends an option (index = the current count) and returns its row.
	UiNodeId AddRadioOption(UiDocument& document, UiNodeId group, std::string label);

} // namespace Swim::UI
