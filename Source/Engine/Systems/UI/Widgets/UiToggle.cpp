#include "Engine/Systems/UI/Widgets/UiToggle.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::UI
{

	namespace
	{

		// Seconds the knob takes from one end to the other at the least.
		constexpr float MinimumKnobSeconds = 1e-4f;
		// Pointer travel (logical units) that turns a press into a knob drag.
		constexpr float KnobDragThreshold = 3.0f;

	} // namespace

	using Internal::Along;
	using Internal::Horizontal;
	using Internal::Length;

	void UiToggleControl::Validate(const UiControlContext& context, const UiControl& control) const
	{
		const auto& parts = control.Parts;

		if (parts.Thumb && (!parts.Track || context.GetParent(parts.Thumb) != parts.Track))
		{
			throw std::invalid_argument("A toggle's knob (Thumb) must be a child of its Track");
		}
	}

	float UiToggleControl::Target(const UiControlContext& context) const
	{
		return context.Control().Check == UiCheckState::Checked ? 1.0f : 0.0f;
	}

	void UiToggleControl::OnAttached(UiControlContext& context)
	{
		knob = Target(context);
		moved = false;
	}

	void UiToggleControl::OnCheckedFromCode(UiControlContext& context)
	{
		if (!context.IsDragging())
		{
			knob = Target(context); // From code: no easing.
		}
	}

	UiPointerResponse UiToggleControl::OnPointerDown(UiControlContext& context, const UiPointerInput& pointer)
	{
		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		const float axis = Internal::Axis(c, pointer.Position);
		moved = false;
		dragStart = axis;

		if (c.ReadOnly)
		{
			return UiPointerResponse::Ignore;
		}

		const auto thumb = c.Parts.Thumb;
		const bool hasKnob = thumb && context.Contains(thumb) && context.IsLaidOut(thumb);

		if (hasKnob)
		{
			const auto bounds = context.GetBounds(thumb);
			const float start = Along(bounds, horizontal);
			const float length = Length(bounds, horizontal);
			grab = axis >= start && axis < start + length ? axis - start : length * 0.5f;
		}
		else
		{
			grab = 0.0f;
		}

		return UiPointerResponse::Capture;
	}

	void UiToggleControl::OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer)
	{
		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		const float axis = Internal::Axis(c, pointer.Position);

		if (std::abs(axis - dragStart) > KnobDragThreshold)
		{
			moved = true;
		}

		if (!moved || !c.Parts.Track || !context.Contains(c.Parts.Track) || !c.Parts.Thumb || !context.Contains(c.Parts.Thumb))
		{
			return;
		}

		const UiRect inner = context.GetContentBox(c.Parts.Track);
		const float size = Length(context.GetDesiredSize(c.Parts.Thumb), horizontal);
		const float travel = std::max(0.0f, Length(inner, horizontal) - size);
		float t = travel > 0.0f ? std::clamp((axis - grab - Along(inner, horizontal)) / travel, 0.0f, 1.0f) : knob;

		if (!horizontal)
		{
			t = 1.0f - t;
		}

		if (t != knob)
		{
			knob = t;
			context.InvalidateArrange();
		}
	}

	void UiToggleControl::EndDrag(UiControlContext& context)
	{
		const auto& c = context.Control();

		if (moved && !c.ReadOnly)
		{
			const auto state = knob >= 0.5f ? UiCheckState::Checked : UiCheckState::Unchecked;

			if (state != c.Check)
			{
				context.ChangeCheck(state);
			}

			context.InvalidateArrange(); // The knob eases (or snaps) to the final state.
			context.InvalidateVisuals(context.GetNode());
		}
	}

	void UiToggleControl::OnPointerUp(UiControlContext& context, bool inside, bool captured)
	{
		const bool dragged = captured && moved;

		if (captured)
		{
			EndDrag(context);
		}

		if (inside && !dragged)
		{
			Flip(context);
		}

		moved = false;
	}

	void UiToggleControl::OnPointerCancel(UiControlContext& context, bool captured)
	{
		if (captured)
		{
			EndDrag(context);
		}

		moved = false;
	}

	std::optional<UiRect> UiToggleControl::PlacePart(
		const UiControlContext& context, UiNodeId part, UiPartRole role, float, const UiRect& inner) const
	{
		const auto& c = context.Control();

		if (role != UiPartRole::Thumb || context.GetParent(part) != c.Parts.Track)
		{
			return std::nullopt;
		}

		const UiPoint size = context.GetDesiredSize(part);

		if (Horizontal(c))
		{
			return UiRect{ knob * std::max(0.0f, inner.Width - size.X), (inner.Height - size.Y) * 0.5f, size.X, size.Y };
		}

		return UiRect{ (inner.Width - size.X) * 0.5f, (1.0f - knob) * std::max(0.0f, inner.Height - size.Y), size.X, size.Y };
	}

	bool UiToggleControl::OnUpdate(UiControlContext& context, float seconds)
	{
		if (context.IsDragging() && moved)
		{
			return false; // The pointer places the knob.
		}

		const float target = Target(context);

		if (knob == target)
		{
			return false;
		}

		const auto& c = context.Control();
		float duration = context.GetStyle(context.GetNode()).TransitionSeconds;

		if (c.Parts.Thumb && context.Contains(c.Parts.Thumb))
		{
			duration = context.GetStyle(c.Parts.Thumb).TransitionSeconds;
		}

		const float step = duration > MinimumKnobSeconds ? seconds / duration : 1.0f;
		knob = knob < target ? std::min(target, knob + step) : std::max(target, knob - step);
		context.InvalidateArrange();
		return knob != target;
	}

	bool UiToggleControl::IsAnimating(const UiControlContext& context) const
	{
		return knob != Target(context) && !(context.IsDragging() && moved);
	}

	UiNodeId CreateToggle(UiDocument& document, UiNodeId parent, std::string label, bool on)
	{
		UiStyle row;
		row.Flow = UiFlow::Row;
		row.AlignItems = UiAlign::Center;
		const auto root = Internal::CreateStyled(document, parent, row);
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		const auto track = Internal::CreateStyled(document, root, overlay);
		const auto knob = document.Create(track);
		UiControl control;
		control.Kind = UiControlKind::Toggle;
		control.Check = on ? UiCheckState::Checked : UiCheckState::Unchecked;
		control.Parts.Track = track;
		control.Parts.Thumb = knob;

		if (!label.empty())
		{
			control.Parts.Label = Internal::ThemedLabel(document, root, std::move(label));
		}

		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::Toggle);
		document.SetThemeClass(track, UiThemeClass::ToggleTrack);
		document.SetThemeClass(knob, UiThemeClass::ToggleKnob);
		return root;
	}

	void Internal::RegisterToggle(UiControlRegistry& registry)
	{
		registry.Register<UiToggleControl>("Toggle",
			[](UiDocument& document, UiNodeId parent)
			{
				return CreateToggle(document, parent, "Toggle");
			});
	}

} // namespace Swim::UI
