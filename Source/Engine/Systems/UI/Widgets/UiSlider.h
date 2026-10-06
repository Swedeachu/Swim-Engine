#pragma once

#include "Engine/Systems/UI/UiControlBehavior.h"

#include <cstdint>

namespace Swim::UI
{

	// A value in [Min, Max]: thumb drag (keeping the grab offset), track press (Jump under the
	// pointer, or Page towards it), arrow/Page/Home/End keys, the wheel while focused. Parts:
	// Track, Fill, Thumb and Tick marks (direct children, placed here); Label (any node) shows
	// the value with LabelDecimals and, when editable, applies a typed number.
	class UiSliderControl : public UiControlBehavior
	{

	  public:

		bool AcceptsTicks() const override { return true; }

		bool LabelMayBeOutside() const override { return true; }

		void Validate(const UiControlContext& context, const UiControl& control) const override;

		void OnAttached(UiControlContext& context) override;

		void OnValueChanged(UiControlContext& context) override;

		UiPointerResponse OnPointerDown(UiControlContext& context, const UiPointerInput& pointer) override;

		void OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer) override;

		void OnPointerUp(UiControlContext& context, bool inside, bool captured) override;

		void OnPointerCancel(UiControlContext& context, bool captured) override;

		bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers modifiers) override;

		bool WantsWheel(const UiControlContext& context) const override { return context.IsFocused(); }

		bool OnWheel(UiControlContext& context, UiPoint delta) override;

		void OnPartCommit(UiControlContext& context, UiNodeId part) override;

		std::optional<UiRect> PlacePart(
			const UiControlContext& context, UiNodeId part, UiPartRole role, float partValue, const UiRect& inner) const override;

	  private:

		// Writes the value into the Label part (an editable label keeps typed text while focused).
		void SyncLabel(UiControlContext& context, bool force) const;

		float grab = 0.0f;
		float pressValue = 0.0f;

	};

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

} // namespace Swim::UI
