#pragma once

#include "Engine/Systems/UI/UiControlBehavior.h"

#include <cstdint>

namespace Swim::UI
{

	// Base of selection owners (radio groups, list views, dropdowns, custom pickers): one
	// selected index (UiControl::Value, -1 for none) among Option nodes registered with
	// SetPartRole(option, owner, UiPartRole::Option, index). Options are separate nodes with
	// the "Option" behaviour; pressing one focuses the owner and selects through it.
	class UiSelectionControl : public UiControlBehavior
	{

	  public:

		bool OwnsOptions() const override { return true; }

		void Validate(const UiControlContext& context, const UiControl& control) const override;

		// From code: rounded and clamped to [-1, count - 1].
		float ClampValue(const UiControlContext& context, float value) const override;

		// Input selection: clamps to [-1, count - 1]; ValueChanged (+ ValueCommitted) when it
		// changed; reveals the option in ScrollTarget.
		bool Select(UiControlContext& context, std::int32_t index, bool commit);

		// Scrolls ScrollTarget so the option at index shows (by ItemExtent, or its bounds).
		void Reveal(UiControlContext& context, std::int32_t index) const;

		std::int32_t GetSelected(const UiControlContext& context) const;

		// --- Hooks the options call on their owner ---
		virtual void OnOptionPressed(UiControlContext& context, std::int32_t index);

		virtual void OnOptionHovered(UiControlContext& context, std::int32_t index) {}

		// The states an option shows: Checked while selected; Focused while it is what keys move
		// (the selection of a focused owner, or a dropdown's highlight); ReadOnly; Disabled.
		virtual UiState GetOptionState(const UiControlContext& context, std::int32_t index) const;

	  protected:

		// Options laid out along x (horizontal list views).
		virtual bool IsHorizontalList(const UiControlContext& context) const { return false; }

	};

	// The behaviour of an option node: hit-testable, never focusable; forwards to its owner.
	class UiOptionControl final : public UiControlBehavior
	{

	  public:

		bool IsFocusable() const override { return false; }

		UiNodeId GetFocusTarget(const UiControlContext& context) const override;

		void OnPointerUp(UiControlContext& context, bool inside, bool captured) override;

		void OnPointerEnter(UiControlContext& context) override;

		UiState GetStateFlags(const UiControlContext& context) const override;

		void SetValue(UiControlContext& context, float value) override;

	};

	// A single-line text option row of an owner (list and dropdown rows), themed MenuItem.
	UiNodeId CreateTextOption(UiDocument& document, UiNodeId parent, UiNodeId owner, std::string text);

} // namespace Swim::UI
