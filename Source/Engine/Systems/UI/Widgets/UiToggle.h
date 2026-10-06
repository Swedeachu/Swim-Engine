#pragma once

#include "Engine/Systems/UI/Widgets/UiCheckbox.h"

#include <string>

namespace Swim::UI
{

	// An on/off switch: click or Enter/Space flips it; dragging the knob (Thumb, a child of the
	// Track) past half flips it on release. The knob eases over its TransitionSeconds.
	class UiToggleControl final : public UiCheckableControl
	{

	  public:

		bool WantsUpdates() const override { return true; }

		void Validate(const UiControlContext& context, const UiControl& control) const override;

		void OnAttached(UiControlContext& context) override;

		UiPointerResponse OnPointerDown(UiControlContext& context, const UiPointerInput& pointer) override;

		void OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer) override;

		void OnPointerUp(UiControlContext& context, bool inside, bool captured) override;

		void OnPointerCancel(UiControlContext& context, bool captured) override;

		std::optional<UiRect> PlacePart(
			const UiControlContext& context, UiNodeId part, UiPartRole role, float partValue, const UiRect& inner) const override;

		bool OnUpdate(UiControlContext& context, float seconds) override;

		bool IsAnimating(const UiControlContext& context) const override;

		float GetKnob() const { return knob; } // Displayed knob position, 0 (off) .. 1 (on).

	  protected:

		bool AllowsMixed() const override { return false; }

		void OnCheckedFromCode(UiControlContext& context) override;

	  private:

		// Ends a knob drag: a dragged knob decides the state by the half it rests in.
		void EndDrag(UiControlContext& context);

		float Target(const UiControlContext& context) const;

		float knob = 0.0f;
		float grab = 0.0f;
		float dragStart = 0.0f;
		bool moved = false;

	};

	struct UiToggleDesc
	{
		std::string Label;
		bool On = false;
	};

	// Row [track [knob], label].
	UiNodeId CreateToggle(UiDocument& document, UiNodeId parent, std::string label, bool on = false);

	inline UiNodeId CreateToggle(UiDocument& document, UiNodeId parent, const UiToggleDesc& desc)
	{
		return CreateToggle(document, parent, desc.Label, desc.On);
	}

} // namespace Swim::UI
