#pragma once

#include "Engine/Systems/UI/UiControlBehavior.h"

namespace Swim::UI
{

	// Mirrors ScrollTarget's scroll offset on one axis: thumb drag, track press (Page by the
	// viewport, or Jump), step buttons (repeating while held), the wheel over the bar, keys
	// when focusable (Style.Focusable). Auto bars hide without overflow; Overlay bars also fade
	// out after FadeDelaySeconds without scrolling or hover. Parts: Thumb, Decrement, Increment
	// (direct children, placed here).
	class UiScrollBarControl : public UiControlBehavior
	{

	  public:

		bool IsFocusable() const override { return false; } // Only with Style.Focusable.

		bool UsesScrollTarget() const override { return true; }

		bool WantsUpdates() const override { return true; }

		bool WantsArranged() const override { return true; }

		void Validate(const UiControlContext& context, const UiControl& control) const override;

		void OnAttached(UiControlContext& context) override;

		void OnValueChanged(UiControlContext& context) override;

		void SetValue(UiControlContext& context, float value) override;

		float GetValue(const UiControlContext& context) const override;

		UiPointerResponse OnPointerDown(UiControlContext& context, const UiPointerInput& pointer) override;

		void OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer) override;

		void OnPointerUp(UiControlContext& context, bool inside, bool captured) override;

		void OnPointerCancel(UiControlContext& context, bool captured) override;

		bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers modifiers) override;

		bool WantsWheel(const UiControlContext&) const override { return true; }

		bool OnWheel(UiControlContext& context, UiPoint delta) override;

		void OnArranged(UiControlContext& context) override;

		std::optional<UiRect> PlacePart(
			const UiControlContext& context, UiNodeId part, UiPartRole role, float partValue, const UiRect& inner) const override;

		bool OnUpdate(UiControlContext& context, float seconds) override;

		bool IsAnimating(const UiControlContext& context) const override;

	  private:

		// The thumb's track along the axis (after the step buttons), relative to the content box.
		std::pair<float, float> Track(const UiControlContext& context, const UiRect& inner) const;

		float ViewportLength(const UiControlContext& context) const;

		void Step(UiControlContext& context, float direction) const;

		float grab = 0.0f;
		float pressValue = 0.0f;
		float activity = 0.0f; // Seconds since the last scroll or hover (overlay fade).
		float opacity = 1.0f;
		bool stepping = false; // A step button is held (repeats in OnUpdate).
		float stepDirection = 0.0f;
		float stepHeld = 0.0f;
		float nextStep = 0.0f;

	};

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

	struct UiScrollAreaDesc
	{
		UiStyle Style;
		bool Vertical = true;
		bool Horizontal = false;
		UiScrollBarVisibility Visibility = UiScrollBarVisibility::Auto;
		bool StepButtons = false;
	};

	// A root (styled by rootStyle, typically a size) holding a clipped viewport and its
	// scroll bars: in flow next to the viewport, or floating over its edges for Overlay.
	UiScrollArea CreateScrollArea(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, bool vertical = true,
		bool horizontal = false, UiScrollBarVisibility visibility = UiScrollBarVisibility::Auto, bool stepButtons = false);

	inline UiScrollArea CreateScrollArea(UiDocument& document, UiNodeId parent, const UiScrollAreaDesc& desc)
	{
		return CreateScrollArea(document, parent, desc.Style, desc.Vertical, desc.Horizontal, desc.Visibility, desc.StepButtons);
	}

	// A clipped node: children scroll with UiDocument::Wheel/SetScroll.
	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style);

} // namespace Swim::UI
