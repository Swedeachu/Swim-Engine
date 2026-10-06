#pragma once

#include "Engine/Systems/UI/UiControlBehavior.h"

#include <string>

namespace Swim::UI
{

	// What checkboxes and toggles share: a check state flipped by a click inside or by
	// activation (Enter/Space/gamepad A), reported as 0/1 (0.5 Mixed) values, set from code
	// with SetChecked/SetValue.
	class UiCheckableControl : public UiControlBehavior
	{

	  public:

		void OnPointerUp(UiControlContext& context, bool inside, bool captured) override;

		UiActivation OnActivate(UiControlContext& context) override;

		void SetValue(UiControlContext& context, float value) override;

		float GetValue(const UiControlContext& context) const override;

		void SetChecked(UiControlContext& context, UiCheckState state) override;

	  protected:

		virtual bool AllowsMixed() const { return true; }

		// Input: flips Checked/Unchecked (Mixed turns Checked) and reports it.
		void Flip(UiControlContext& context) const;

		// The check state changed from code (subclasses snap animations).
		virtual void OnCheckedFromCode(UiControlContext& context) {}

	};

	// Unchecked / Checked / Mixed (Mixed only from code). Parts: Track (the box), Mark, Mixed, Label.
	class UiCheckboxControl final : public UiCheckableControl
	{
	};

	struct UiCheckboxDesc
	{
		std::string Label;
		UiCheckState State = UiCheckState::Unchecked;
	};

	// Row [box [mark, mixed mark], label]; an empty label creates no label part.
	UiNodeId CreateCheckbox(UiDocument& document, UiNodeId parent, std::string label, UiCheckState state = UiCheckState::Unchecked);

	inline UiNodeId CreateCheckbox(UiDocument& document, UiNodeId parent, const UiCheckboxDesc& desc)
	{
		return CreateCheckbox(document, parent, desc.Label, desc.State);
	}

} // namespace Swim::UI
