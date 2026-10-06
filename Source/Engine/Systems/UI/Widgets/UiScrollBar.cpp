#include "Engine/Systems/UI/Widgets/UiScrollBar.h"

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

		// Keyboard/wheel/step-button step without a Step, logical units.
		constexpr float LineStep = 40.0f;
		// Held step buttons repeat after this delay, at this interval.
		constexpr float StepRepeatDelay = 0.4f;
		constexpr float StepRepeatInterval = 0.05f;

	} // namespace

	using Internal::Along;
	using Internal::Horizontal;
	using Internal::Length;

	void UiScrollBarControl::Validate(const UiControlContext& context, const UiControl& control) const
	{
		const auto node = context.GetNode();
		const auto directChild = [&](UiNodeId part)
		{
			return !part || context.GetParent(part) == node;
		};

		if (!directChild(control.Parts.Thumb) || !directChild(control.Parts.Decrement) || !directChild(control.Parts.Increment))
		{
			throw std::invalid_argument("A scroll bar's thumb and step buttons must be its children");
		}

		if (control.ScrollTarget && context.IsInside(control.ScrollTarget, node))
		{
			throw std::invalid_argument("A scroll bar's target must be another node outside the bar");
		}
	}

	void UiScrollBarControl::OnAttached(UiControlContext&)
	{
		activity = 0.0f;
		opacity = 1.0f;
		stepping = false;
	}

	float UiScrollBarControl::ViewportLength(const UiControlContext& context) const
	{
		const auto& c = context.Control();
		return Length(context.GetContentBox(c.ScrollTarget), Horizontal(c));
	}

	void UiScrollBarControl::OnValueChanged(UiControlContext& context)
	{
		const auto& c = context.Control();

		if (c.ScrollTarget && context.Contains(c.ScrollTarget))
		{
			context.SetScroll(c.ScrollTarget, Horizontal(c), c.Value);
			activity = 0.0f; // Overlay bars reappear while scrolling.
		}
	}

	void UiScrollBarControl::SetValue(UiControlContext& context, float value)
	{
		const auto& c = context.Control();

		if (c.ScrollTarget && context.Contains(c.ScrollTarget))
		{
			// The target's offset; the next Layout clamps it and syncs the bar.
			auto scroll = context.GetScroll(c.ScrollTarget);
			(Horizontal(c) ? scroll.X : scroll.Y) = std::isfinite(value) ? value : 0.0f;
			context.GetDocument().SetScroll(c.ScrollTarget, scroll);
		}
	}

	float UiScrollBarControl::GetValue(const UiControlContext& context) const
	{
		const auto& c = context.Control();

		if (c.ScrollTarget && context.Contains(c.ScrollTarget))
		{
			const auto scroll = context.GetScroll(c.ScrollTarget);
			return Horizontal(c) ? scroll.X : scroll.Y;
		}

		return c.Value;
	}

	std::pair<float, float> UiScrollBarControl::Track(const UiControlContext& context, const UiRect& inner) const
	{
		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		const float cross = horizontal ? inner.Height : inner.Width;
		const auto button = [&](UiNodeId id)
		{
			return id && context.Contains(id) && context.GetStyle(id).Visible && context.GetParent(id) == context.GetNode() ? cross : 0.0f;
		};
		const float length = Length(inner, horizontal);
		const float start = std::min(button(c.Parts.Decrement), length);
		const float end = std::min(button(c.Parts.Increment), length - start);
		return { start, length - start - end };
	}

	void UiScrollBarControl::Step(UiControlContext& context, float direction) const
	{
		const auto& c = context.Control();
		context.ChangeValue(c.Value + direction * (c.Step > 0.0f ? c.Step : LineStep), true);
	}

	UiPointerResponse UiScrollBarControl::OnPointerDown(UiControlContext& context, const UiPointerInput& pointer)
	{
		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		const float axis = Internal::Axis(c, pointer.Position);
		pressValue = c.Value;

		if (c.ReadOnly)
		{
			return UiPointerResponse::Ignore;
		}

		// Step buttons: one step now, repeats while held (OnUpdate).
		for (const auto& [button, direction] : { std::pair{ c.Parts.Decrement, -1.0f }, std::pair{ c.Parts.Increment, 1.0f } })
		{
			if (button && context.Contains(button) && context.IsLaidOut(button) && context.GetBounds(button).Contains(pointer.Position))
			{
				Step(context, direction);
				stepping = true;
				stepDirection = direction;
				stepHeld = 0.0f;
				nextStep = StepRepeatDelay;
				return UiPointerResponse::Handled;
			}
		}

		const bool hasThumb = c.Parts.Thumb && context.Contains(c.Parts.Thumb) && context.IsLaidOut(c.Parts.Thumb);
		const auto thumb = hasThumb ? context.GetBounds(c.Parts.Thumb) : UiRect{};
		const float thumbStart = hasThumb ? Along(thumb, horizontal) : axis;
		const float thumbLength = hasThumb ? Length(thumb, horizontal) : 0.0f;

		if (hasThumb && axis >= thumbStart && axis < thumbStart + thumbLength)
		{
			grab = axis - thumbStart;
			return UiPointerResponse::Capture;
		}

		if (c.TrackClick == UiTrackClick::Page)
		{
			const float page = c.PageStep > 0.0f								   ? c.PageStep
				: c.ScrollTarget && context.Contains(c.ScrollTarget) ? ViewportLength(context)
																	   : (c.Max - c.Min) * 0.1f;
			const bool increase = axis >= thumbStart + thumbLength * 0.5f; // Towards the pointer.
			context.ChangeValue(c.Value + (increase ? page : -page), true);
			return UiPointerResponse::Handled;
		}

		grab = thumbLength * 0.5f;
		OnPointerDrag(context, pointer);
		return UiPointerResponse::Capture;
	}

	void UiScrollBarControl::OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer)
	{
		const auto& c = context.Control();

		if (!c.Parts.Thumb || !context.Contains(c.Parts.Thumb))
		{
			return;
		}

		const bool horizontal = Horizontal(c);
		const UiRect inner = context.GetContentBox(context.GetNode());
		const auto [trackStart, trackLength] = Track(context, inner);
		const float thumbLength = Length(context.GetBounds(c.Parts.Thumb), horizontal);
		const float travel = std::max(0.0f, trackLength - thumbLength);
		const float axis = Internal::Axis(c, pointer.Position);
		const float t = travel > 0.0f ? std::clamp((axis - grab - Along(inner, horizontal) - trackStart) / travel, 0.0f, 1.0f) : 0.0f;
		context.ChangeValue(t * c.Max, false);
	}

	void UiScrollBarControl::OnPointerUp(UiControlContext& context, bool, bool captured)
	{
		stepping = false;

		if (captured)
		{
			context.Commit(pressValue);
		}
	}

	void UiScrollBarControl::OnPointerCancel(UiControlContext& context, bool captured)
	{
		stepping = false;

		if (captured)
		{
			context.Commit(pressValue);
		}
	}

	bool UiScrollBarControl::OnKey(UiControlContext& context, UiKey key, UiKeyModifiers)
	{
		const auto& c = context.Control();

		if (c.ReadOnly)
		{
			return false;
		}

		const bool horizontal = Horizontal(c);
		const float step = c.Step > 0.0f ? c.Step : LineStep;
		float page = c.PageStep;

		if (page <= 0.0f)
		{
			page = c.ScrollTarget && context.Contains(c.ScrollTarget) ? ViewportLength(context) : LineStep;
		}

		// Scroll bars scroll towards larger offsets right and down.
		float delta = 0.0f;

		switch (key)
		{
		case UiKey::Left: delta = horizontal ? -step : 0.0f; break;
		case UiKey::Right: delta = horizontal ? step : 0.0f; break;
		case UiKey::Up: delta = horizontal ? 0.0f : -step; break;
		case UiKey::Down: delta = horizontal ? 0.0f : step; break;
		case UiKey::PageUp: delta = -page; break;
		case UiKey::PageDown: delta = page; break;
		case UiKey::Home: context.ChangeValue(c.Min, true); return true;
		case UiKey::End: context.ChangeValue(c.Max, true); return true;
		default: return false;
		}

		if (delta == 0.0f)
		{
			return false; // The cross axis navigates.
		}

		context.ChangeValue(c.Value + delta, true);
		return true;
	}

	bool UiScrollBarControl::OnWheel(UiControlContext& context, UiPoint delta)
	{
		const auto& c = context.Control();

		if (c.ReadOnly)
		{
			return false;
		}

		const float amount = Horizontal(c) && delta.X != 0.0f ? delta.X : delta.Y;
		return context.ChangeValue(c.Value + amount, true);
	}

	void UiScrollBarControl::OnArranged(UiControlContext& context)
	{
		// Arrange visits bars and targets in document order, so a bar placed before its target
		// saw last frame's extent: refresh from the final target and re-place the thumb.
		const auto node = context.GetNode();

		if (!context.IsLaidOut(node))
		{
			return;
		}

		auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		float maximum = 0.0f;
		float offset = 0.0f;

		if (c.ScrollTarget && context.Contains(c.ScrollTarget))
		{
			const auto max = context.GetMaxScroll(c.ScrollTarget);
			const auto scroll = context.GetScroll(c.ScrollTarget);
			maximum = horizontal ? max.X : max.Y;
			offset = horizontal ? scroll.X : scroll.Y;
		}

		if (offset != c.Value || maximum != c.Max)
		{
			activity = 0.0f; // Overlay bars reappear while scrolling.
		}

		c.Min = 0.0f;
		c.Max = maximum;
		c.Value = offset;
		context.SetHidden(c.Visibility != UiScrollBarVisibility::Always && maximum <= 0.0f);

		if (c.Parts.Thumb && context.Contains(c.Parts.Thumb) && context.GetParent(c.Parts.Thumb) == node)
		{
			context.PlacePartNow(c.Parts.Thumb);
		}
	}

	std::optional<UiRect> UiScrollBarControl::PlacePart(
		const UiControlContext& context, UiNodeId part, UiPartRole role, float, const UiRect& inner) const
	{
		if (context.GetParent(part) != context.GetNode())
		{
			return std::nullopt;
		}

		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);

		if (role == UiPartRole::Decrement || role == UiPartRole::Increment)
		{
			const float cross = horizontal ? inner.Height : inner.Width;
			const float along = role == UiPartRole::Decrement ? 0.0f : std::max(0.0f, Length(inner, horizontal) - cross);
			return horizontal ? UiRect{ along, 0.0f, cross, inner.Height } : UiRect{ 0.0f, along, inner.Width, cross };
		}

		if (role != UiPartRole::Thumb)
		{
			return std::nullopt;
		}

		const auto [trackStart, track] = Track(context, inner);
		float viewport = track;
		float maximum = 0.0f;
		float offset = 0.0f;

		if (c.ScrollTarget && context.Contains(c.ScrollTarget))
		{
			viewport = ViewportLength(context);
			const auto max = context.GetMaxScroll(c.ScrollTarget);
			const auto scroll = context.GetScroll(c.ScrollTarget);
			maximum = horizontal ? max.X : max.Y;
			offset = horizontal ? scroll.X : scroll.Y;
		}

		float length = track;

		if (maximum > 0.0f && viewport + maximum > 0.0f)
		{
			length = std::clamp(track * viewport / (viewport + maximum), std::min(c.MinThumbLength, track), track);
		}

		const float position = trackStart + (maximum > 0.0f ? std::clamp(offset / maximum, 0.0f, 1.0f) * (track - length) : 0.0f);
		return horizontal ? UiRect{ position, 0.0f, length, inner.Height } : UiRect{ 0.0f, position, inner.Width, length };
	}

	bool UiScrollBarControl::OnUpdate(UiControlContext& context, float seconds)
	{
		bool animating = false;

		if (stepping && !context.IsPressed())
		{
			stepping = false; // Released or cancelled.
		}

		if (stepping)
		{
			stepHeld += seconds;

			while (stepHeld >= nextStep)
			{
				Step(context, stepDirection);
				nextStep += StepRepeatInterval;
			}

			animating = true;
		}

		const auto& c = context.Control();

		if (c.Visibility == UiScrollBarVisibility::Overlay)
		{
			activity = context.IsHovered() || context.IsDragging() ? 0.0f : activity + seconds;
			float target = 1.0f;

			if (activity > c.FadeDelaySeconds)
			{
				target = c.FadeSeconds > 0.0f ? std::max(0.0f, 1.0f - (activity - c.FadeDelaySeconds) / c.FadeSeconds) : 0.0f;
			}

			opacity = target;
			context.SetOpacity(opacity);
			animating = animating || (!context.IsHidden() && opacity > 0.0f);
		}
		else if (opacity != 1.0f)
		{
			opacity = 1.0f;
			context.SetOpacity(1.0f);
		}

		return animating;
	}

	bool UiScrollBarControl::IsAnimating(const UiControlContext& context) const
	{
		const auto& c = context.Control();
		return c.Visibility == UiScrollBarVisibility::Overlay && !context.IsHidden() && opacity > 0.0f &&
			(opacity < 1.0f || activity <= c.FadeDelaySeconds + c.FadeSeconds);
	}

	UiNodeId CreateScrollBar(UiDocument& document, UiNodeId parent, UiNodeId target, const UiScrollBarDesc& desc)
	{
		UiStyle bar;
		bar.Flow = UiFlow::Overlay;
		const auto root = Internal::CreateStyled(document, parent, bar);
		const auto thumb = document.Create(root);
		const auto decrement = desc.StepButtons ? document.Create(root) : UiNodeId{};
		const auto increment = desc.StepButtons ? document.Create(root) : UiNodeId{};
		UiControl control;
		control.Kind = UiControlKind::ScrollBar;
		control.Orientation = desc.Orientation;
		control.Visibility = desc.Visibility;
		control.TrackClick = desc.TrackClick;
		control.ScrollTarget = target;
		control.MinThumbLength = Internal::ThemeOf(document).Metrics.ScrollBarMinThumb;
		control.Parts.Thumb = thumb;
		control.Parts.Decrement = decrement;
		control.Parts.Increment = increment;
		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::ScrollBar);
		document.SetThemeClass(thumb, UiThemeClass::ScrollThumb);

		for (const auto button : { decrement, increment })
		{
			if (button)
			{
				document.SetThemeClass(button, UiThemeClass::ScrollButton);
			}
		}

		return root;
	}

	UiScrollArea CreateScrollArea(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, bool vertical, bool horizontal,
		UiScrollBarVisibility visibility, bool stepButtons)
	{
		UiScrollArea area;
		const bool overlay = visibility == UiScrollBarVisibility::Overlay;
		const float thickness = Internal::ThemeOf(document).Metrics.ScrollBarThickness;
		auto root = rootStyle;
		root.Flow = overlay ? UiFlow::Overlay : UiFlow::Column;
		root.AlignItems = UiAlign::Stretch;
		area.Root = Internal::CreateStyled(document, parent, root);
		UiNodeId row = area.Root;

		if (!overlay)
		{
			UiStyle rowStyle;
			rowStyle.Flow = UiFlow::Row;
			rowStyle.AlignItems = UiAlign::Stretch;
			rowStyle.Grow = 1.0f;
			rowStyle.Shrink = 1.0f;
			row = Internal::CreateStyled(document, area.Root, rowStyle);
		}

		UiStyle viewport;
		viewport.Clip = true;
		viewport.Grow = 1.0f;
		viewport.Shrink = 1.0f;
		viewport.AlignSelf = UiAlign::Stretch;
		area.Viewport = Internal::CreateStyled(document, row, viewport);
		UiScrollBarDesc barDesc;
		barDesc.Visibility = visibility;
		barDesc.StepButtons = stepButtons;
		const auto place = [&](UiNodeId bar, bool isVertical)
		{
			if (!overlay)
			{
				return;
			}

			auto style = document.GetStyle(bar);
			style.Absolute = true;
			style.AnchorMin = isVertical ? UiPoint{ 1.0f, 0.0f } : UiPoint{ 0.0f, 1.0f };
			style.AnchorMax = isVertical ? UiPoint{ 1.0f, 1.0f } : UiPoint{ 1.0f, 1.0f };
			style.Pivot = isVertical ? UiPoint{ 1.0f, 0.0f } : UiPoint{ 0.0f, 1.0f };

			if (vertical && horizontal)
			{
				(isVertical ? style.Margin.Bottom : style.Margin.Right) = thickness;
			}

			document.SetStyle(bar, style);
		};

		if (vertical)
		{
			barDesc.Orientation = UiOrientation::Vertical;
			area.Vertical = CreateScrollBar(document, row, area.Viewport, barDesc);
			place(area.Vertical, true);
		}

		if (horizontal)
		{
			barDesc.Orientation = UiOrientation::Horizontal;
			area.Horizontal = CreateScrollBar(document, area.Root, area.Viewport, barDesc);
			place(area.Horizontal, false);

			if (!overlay && vertical)
			{
				auto style = document.GetStyle(area.Horizontal);
				style.Margin.Right = thickness; // Leaves the corner under the vertical bar.
				document.SetStyle(area.Horizontal, style);
			}
		}

		return area;
	}

	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style)
	{
		auto clipped = style;
		clipped.Clip = true;
		return Internal::CreateStyled(document, parent, clipped);
	}

	void Internal::RegisterScrollBar(UiControlRegistry& registry)
	{
		registry.Register<UiScrollBarControl>("ScrollBar");
	}

} // namespace Swim::UI
