#pragma once

#include "Engine/Systems/UI/Widgets/UiMenu.h"
#include "Engine/Systems/UI/Widgets/UiSelection.h"

#include <string>
#include <vector>

namespace Swim::UI
{

	// A closed choice that opens its option list (Parts.Popup, a root child) below itself:
	// click or Enter/Space opens; closed, Up/Down/Home/End select directly; open, they move a
	// highlight that Enter/Space commits; hovering an option moves the highlight; Escape, a
	// press outside or focus leaving closes it. The Label part shows the choice.
	class UiDropdownControl final : public UiSelectionControl
	{

	  public:

		bool UsesScrollTarget() const override { return true; }

		bool UsesPopup() const override { return true; }

		bool OptionsMayBeOutside() const override { return true; } // In its popup.

		void OnAttached(UiControlContext& context) override;

		void OnValueChanged(UiControlContext& context) override;

		void OnPointerUp(UiControlContext& context, bool inside, bool captured) override;

		bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers modifiers) override;

		UiActivation OnActivate(UiControlContext& context) override;

		void OnBlur(UiControlContext& context, UiNodeId newFocus) override;

		void OnPopupLaidOut(UiControlContext& context, UiNodeId popup) override;

		void OnOptionPressed(UiControlContext& context, std::int32_t index) override;

		void OnOptionHovered(UiControlContext& context, std::int32_t index) override;

		UiState GetOptionState(const UiControlContext& context, std::int32_t index) const override;

		bool IsOpen(const UiControlContext& context) const;

		// Opens the list below the dropdown, or closes it.
		void Toggle(UiControlContext& context);

		std::int32_t GetHighlight() const { return highlight; }

	  private:

		// The Label shows the selected option's text (or keeps its placeholder).
		void ShowSelection(UiControlContext& context) const;

		std::int32_t highlight = -1;

	};

	struct UiDropdown
	{
		UiNodeId Root;	// The Dropdown control: a row [label, arrow].
		UiNodeId Label; // Shows the selected option's text (or the placeholder).
		UiNodeId Arrow;
		UiPopupList List; // Opened below Root by activation; options are its Items' children.
	};

	struct UiDropdownDesc
	{
		std::vector<std::string> Options;
		std::int32_t Selected = -1;
		std::string Placeholder;
	};

	UiDropdown CreateDropdown(UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected = -1,
		std::string placeholder = {});

	inline UiDropdown CreateDropdown(UiDocument& document, UiNodeId parent, const UiDropdownDesc& desc)
	{
		return CreateDropdown(document, parent, desc.Options, desc.Selected, desc.Placeholder);
	}

	UiNodeId AddDropdownOption(UiDocument& document, const UiDropdown& dropdown, std::string text);

} // namespace Swim::UI
