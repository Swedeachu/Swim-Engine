#include "Engine/Systems/UI/Widgets/UiSlider.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace Swim::UI
{

	using Internal::Along;
	using Internal::Horizontal;
	using Internal::Length;

	void UiSliderControl::Validate(const UiControlContext& context, const UiControl& control) const
	{
		const auto node = context.GetNode();
		const auto directChild = [&](UiNodeId part)
		{
			return !part || context.GetParent(part) == node;
		};

		if (!directChild(control.Parts.Track) || !directChild(control.Parts.Fill) || !directChild(control.Parts.Thumb))
		{
			throw std::invalid_argument("Slider track, fill and thumb must be children of the slider");
		}
	}

	void UiSliderControl::OnAttached(UiControlContext& context)
	{
		SyncLabel(context, false);
	}

	void UiSliderControl::OnValueChanged(UiControlContext& context)
	{
		SyncLabel(context, false);
	}

	void UiSliderControl::SyncLabel(UiControlContext& context, bool force) const
	{
		const auto& c = context.Control();
		const auto label = c.Parts.Label;

		if (c.LabelDecimals < 0 || !label || !context.Contains(label))
		{
			return;
		}

		if (!context.HasFonts(label) || (!force && context.IsEditable(label) && label == context.GetFocus()))
		{
			return; // Typing into an editable value is not overwritten.
		}

		char buffer[64];
		const float shown = std::abs(c.Value) < 0.5f * std::pow(10.0f, -float(c.LabelDecimals)) ? 0.0f : c.Value; // No "-0".
		std::snprintf(buffer, sizeof(buffer), "%.*f", int(c.LabelDecimals), double(shown));
		context.SetText(label, buffer);
	}

	UiPointerResponse UiSliderControl::OnPointerDown(UiControlContext& context, const UiPointerInput& pointer)
	{
		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		const float axis = Internal::Axis(c, pointer.Position);
		pressValue = c.Value;

		if (c.ReadOnly)
		{
			return UiPointerResponse::Ignore;
		}

		const bool hasThumb = c.Parts.Thumb && context.Contains(c.Parts.Thumb) && context.IsLaidOut(c.Parts.Thumb);
		const auto thumb = hasThumb ? context.GetBounds(c.Parts.Thumb) : UiRect{};
		const float thumbStart = hasThumb ? Along(thumb, horizontal) : axis;
		const float thumbLength = hasThumb ? Length(thumb, horizontal) : 0.0f;

		if (hasThumb && axis >= thumbStart && axis < thumbStart + thumbLength)
		{
			grab = axis - thumbStart; // The thumb keeps the grab offset.
			return UiPointerResponse::Capture;
		}

		if (c.TrackClick == UiTrackClick::Page)
		{
			const float page = c.PageStep > 0.0f ? c.PageStep : (c.Max - c.Min) * 0.1f;
			// Towards the pointer; vertical sliders grow upwards.
			const bool beyond = axis >= thumbStart + thumbLength * 0.5f;
			const bool increase = horizontal ? beyond : !beyond;
			context.ChangeValue(c.Value + (increase ? page : -page), true);
			return UiPointerResponse::Handled;
		}

		// Jump: the thumb's centre comes under the pointer, then follows it.
		grab = thumbLength * 0.5f;
		OnPointerDrag(context, pointer);
		return UiPointerResponse::Capture;
	}

	void UiSliderControl::OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer)
	{
		const auto& c = context.Control();
		const bool horizontal = Horizontal(c);
		const UiRect inner = context.GetContentBox(context.GetNode());
		const UiPoint thumb = c.Parts.Thumb && context.Contains(c.Parts.Thumb) ? context.GetDesiredSize(c.Parts.Thumb) : UiPoint{};
		const float travel = std::max(0.0f, Length(inner, horizontal) - Length(thumb, horizontal));
		float t = travel > 0.0f ? (Internal::Axis(c, pointer.Position) - grab - Along(inner, horizontal)) / travel : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);

		if (!horizontal)
		{
			t = 1.0f - t; // Min at the bottom.
		}

		context.ChangeValue(c.Min + t * (c.Max - c.Min), false);
	}

	void UiSliderControl::OnPointerUp(UiControlContext& context, bool, bool captured)
	{
		if (captured)
		{
			context.Commit(pressValue);
		}
	}

	void UiSliderControl::OnPointerCancel(UiControlContext& context, bool captured)
	{
		if (captured)
		{
			context.Commit(pressValue); // Keeps the value reached.
		}
	}

	bool UiSliderControl::OnKey(UiControlContext& context, UiKey key, UiKeyModifiers)
	{
		const auto& c = context.Control();

		if (c.ReadOnly)
		{
			return false;
		}

		const bool horizontal = Horizontal(c);
		const float range = c.Max - c.Min;
		const float step = c.Step > 0.0f ? c.Step : range * 0.01f;
		const float page = c.PageStep > 0.0f ? c.PageStep : range * 0.1f;
		// Sliders grow right and up.
		float delta = 0.0f;

		switch (key)
		{
		case UiKey::Left: delta = horizontal ? -step : 0.0f; break;
		case UiKey::Right: delta = horizontal ? step : 0.0f; break;
		case UiKey::Up: delta = horizontal ? 0.0f : step; break;
		case UiKey::Down: delta = horizontal ? 0.0f : -step; break;
		case UiKey::PageUp: delta = page; break;
		case UiKey::PageDown: delta = -page; break;
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

	bool UiSliderControl::OnWheel(UiControlContext& context, UiPoint delta)
	{
		const auto& c = context.Control();

		if (c.ReadOnly || delta.Y == 0.0f)
		{
			return false;
		}

		const float step = c.Step > 0.0f ? c.Step : (c.Max - c.Min) * 0.01f;
		return context.ChangeValue(c.Value + (delta.Y < 0.0f ? step : -step), true); // Wheel up increases.
	}

	void UiSliderControl::OnPartCommit(UiControlContext& context, UiNodeId part)
	{
		const auto& c = context.Control();

		if (part != c.Parts.Label)
		{
			return;
		}

		// Plain decimal numbers, surrounding spaces and a leading '+' allowed; anything else
		// restores the displayed value.
		const std::string typed = context.GetText(part);
		std::string_view text = typed;

		while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
		{
			text.remove_prefix(1);
		}

		while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
		{
			text.remove_suffix(1);
		}

		if (!text.empty() && text.front() == '+')
		{
			text.remove_prefix(1);
		}

		float parsed = 0.0f;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);

		if (!text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(parsed) && !c.ReadOnly &&
			context.IsAvailable())
		{
			context.ChangeValue(parsed, true);
		}

		// Reformat (also when the value did not change, or the text was rejected).
		context.SetText(part, {});
		SyncLabel(context, true);

		if (context.GetText(part).empty())
		{
			context.SetText(part, typed); // Not a formatted label (no decimals or fonts).
		}
	}

	std::optional<UiRect> UiSliderControl::PlacePart(
		const UiControlContext& context, UiNodeId part, UiPartRole role, float partValue, const UiRect& inner) const
	{
		if (context.GetParent(part) != context.GetNode())
		{
			return std::nullopt;
		}

		const auto& c = context.Control();
		const UiPoint thumb = c.Parts.Thumb && context.Contains(c.Parts.Thumb) ? context.GetDesiredSize(c.Parts.Thumb) : UiPoint{};
		const UiPoint size = context.GetDesiredSize(part);
		const float range = c.Max - c.Min;
		const float t = range > 0.0f ? (c.Value - c.Min) / range : 0.0f;
		const float tick = range > 0.0f ? std::clamp((partValue - c.Min) / range, 0.0f, 1.0f) : 0.0f;

		if (Horizontal(c))
		{
			const float travel = std::max(0.0f, inner.Width - thumb.X);
			const float start = t * travel;
			const float center = start + thumb.X * 0.5f;
			const float thickness = size.Y;

			switch (role)
			{
			case UiPartRole::Tick: return UiRect{ tick * travel + (thumb.X - size.X) * 0.5f, (inner.Height - thickness) * 0.5f, size.X, thickness };
			case UiPartRole::Track: return UiRect{ 0.0f, (inner.Height - thickness) * 0.5f, inner.Width, thickness };
			case UiPartRole::Fill: return UiRect{ 0.0f, (inner.Height - thickness) * 0.5f, center, thickness };
			case UiPartRole::Thumb: return UiRect{ start, (inner.Height - thumb.Y) * 0.5f, thumb.X, thumb.Y };
			default: return std::nullopt;
			}
		}

		const float travel = std::max(0.0f, inner.Height - thumb.Y);
		const float start = (1.0f - t) * travel; // Min at the bottom.
		const float center = start + thumb.Y * 0.5f;
		const float thickness = size.X;

		switch (role)
		{
		case UiPartRole::Tick: return UiRect{ (inner.Width - thickness) * 0.5f, (1.0f - tick) * travel + (thumb.Y - size.Y) * 0.5f, thickness, size.Y };
		case UiPartRole::Track: return UiRect{ (inner.Width - thickness) * 0.5f, 0.0f, thickness, inner.Height };
		case UiPartRole::Fill: return UiRect{ (inner.Width - thickness) * 0.5f, center, thickness, std::max(0.0f, inner.Height - center) };
		case UiPartRole::Thumb: return UiRect{ (inner.Width - thumb.X) * 0.5f, start, thumb.X, thumb.Y };
		default: return std::nullopt;
		}
	}

	UiNodeId CreateSlider(UiDocument& document, UiNodeId parent, const UiSliderDesc& desc)
	{
		if (desc.ShowValue)
		{
			UiStyle row;
			row.Flow = UiFlow::Row;
			row.AlignItems = UiAlign::Center;
			row.Gap = Internal::ThemeOf(document).Metrics.Spacing;
			parent = Internal::CreateStyled(document, parent, row);
		}

		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		const auto root = Internal::CreateStyled(document, parent, overlay);
		const auto track = document.Create(root);
		std::vector<std::pair<UiNodeId, float>> ticks;

		for (std::uint32_t i = 0; desc.Ticks >= 2 && i < desc.Ticks; ++i)
		{
			ticks.emplace_back(document.Create(root), desc.Min + (desc.Max - desc.Min) * float(i) / float(desc.Ticks - 1));
		}

		const auto fill = document.Create(root);
		const auto thumb = document.Create(root);
		UiControl control;
		control.Kind = UiControlKind::Slider;
		control.Orientation = desc.Orientation;
		control.Min = desc.Min;
		control.Max = desc.Max;
		control.Value = desc.Value;
		control.Step = desc.Step;
		control.PageStep = desc.PageStep;
		control.TrackClick = desc.TrackClick;
		control.Parts.Track = track;
		control.Parts.Fill = fill;
		control.Parts.Thumb = thumb;

		if (desc.ShowValue)
		{
			const auto label = document.Create(parent);
			UiStyle end;
			end.TextAlign = Text::TextAlign::End;
			document.SetStyle(label, end);
			document.SetText(label, Internal::ThemeFonts(document), {}, Internal::ThemeTextSize(document, UiThemeClass::SliderValue));
			document.SetThemeClass(label, UiThemeClass::SliderValue);
			control.Parts.Label = label;
			control.LabelDecimals = std::clamp(desc.Decimals, 0, 9);

			if (desc.EditableValue)
			{
				auto field = document.GetStyle(label);
				field.Clip = true;
				field.TextWrap = Text::TextWrap::None;
				document.SetStyle(label, field);
				UiTextEditOptions options;
				options.MaxBytes = 64;
				document.SetEditable(label, true, options);
			}
		}

		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::Slider);
		document.SetThemeClass(track, UiThemeClass::SliderTrack);

		for (const auto& [tick, value] : ticks)
		{
			document.SetPartRole(tick, root, UiPartRole::Tick, value);
			document.SetThemeClass(tick, UiThemeClass::SliderTick);
		}

		document.SetThemeClass(fill, UiThemeClass::SliderFill);
		document.SetThemeClass(thumb, UiThemeClass::SliderThumb);
		return root;
	}

	void Internal::RegisterSlider(UiControlRegistry& registry)
	{
		registry.Register<UiSliderControl>("Slider",
			[](UiDocument& document, UiNodeId parent)
			{
				return CreateSlider(document, parent);
			});
	}

} // namespace Swim::UI
