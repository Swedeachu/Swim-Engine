#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

// Controls (critical-path item 79): checkboxes, toggles, sliders and scroll bars as
// document behaviour on ordinary nodes. Everything here is driven by UiDocument input
// (pointer, wheel, keys, activation), never by the platform.
namespace Swim::UI
{
	namespace
	{
		bool Finite(float value)
		{
			return std::isfinite(value);
		}

		bool Horizontal(const UiControl& control)
		{
			return control.Orientation == UiOrientation::Horizontal;
		}

		float Along(const UiRect& rect, bool horizontal)
		{
			return horizontal ? rect.X : rect.Y;
		}

		float Length(const UiRect& rect, bool horizontal)
		{
			return horizontal ? rect.Width : rect.Height;
		}

		float Length(UiPoint size, bool horizontal)
		{
			return horizontal ? size.X : size.Y;
		}

		// Seconds the toggle knob takes from one end to the other.
		constexpr float MinimumKnobSeconds = 1e-4f;
		// Pointer travel (logical units) that turns a toggle press into a knob drag.
		constexpr float KnobDragThreshold = 3.0f;
		// Keyboard/wheel/step-button step of scroll bars without a Step, logical units.
		constexpr float ScrollBarLineStep = 40.0f;
		// Held scroll bar step buttons repeat after this delay, at this interval.
		constexpr float StepRepeatDelay = 0.4f;
		constexpr float StepRepeatInterval = 0.05f;
	} // namespace

	float UiDocument::Impl::CheckValue(UiCheckState state) const
	{
		return state == UiCheckState::Checked ? 1.0f : state == UiCheckState::Mixed ? 0.5f : 0.0f;
	}

	float UiDocument::Impl::ClampValue(const UiControl& control, float value) const
	{
		if (!Finite(value))
		{
			value = control.Min;
		}
		value = std::clamp(value, control.Min, control.Max);
		if (control.Step > 0.0f)
		{
			value = control.Min + std::round((value - control.Min) / control.Step) * control.Step;
			value = std::clamp(value, control.Min, control.Max);
		}
		return value;
	}

	void UiDocument::Impl::ValidateControl(const Node& node, const UiControl& c) const
	{
		const bool valid = static_cast<std::uint8_t>(c.Kind) <= static_cast<std::uint8_t>(UiControlKind::ScrollBar) &&
			static_cast<std::uint8_t>(c.Orientation) <= static_cast<std::uint8_t>(UiOrientation::Vertical) &&
			static_cast<std::uint8_t>(c.Check) <= static_cast<std::uint8_t>(UiCheckState::Mixed) &&
			static_cast<std::uint8_t>(c.TrackClick) <= static_cast<std::uint8_t>(UiTrackClick::Page) &&
			static_cast<std::uint8_t>(c.Visibility) <= static_cast<std::uint8_t>(UiScrollBarVisibility::Overlay) && Finite(c.Min) &&
			Finite(c.Max) && c.Min <= c.Max && std::abs(c.Min) <= 1e9f && std::abs(c.Max) <= 1e9f && Finite(c.Step) && c.Step >= 0.0f &&
			Finite(c.PageStep) && c.PageStep >= 0.0f && Finite(c.MinThumbLength) && c.MinThumbLength >= 0.0f &&
			c.MinThumbLength <= Internal::MaxLogical && Finite(c.FadeDelaySeconds) && c.FadeDelaySeconds >= 0.0f &&
			c.FadeDelaySeconds <= 60.0f && Finite(c.FadeSeconds) && c.FadeSeconds >= 0.0f && c.FadeSeconds <= 60.0f &&
			c.LabelDecimals >= -1 && c.LabelDecimals <= 9;
		if (!valid)
		{
			throw std::invalid_argument("Invalid UI control");
		}
		const auto isDescendant = [&](UiNodeId id, UiNodeId ancestor)
		{
			for (auto current = Get(id).Parent; current; current = Get(current).Parent)
			{
				if (current == ancestor)
				{
					return true;
				}
			}
			return false;
		};
		const auto& parts = c.Parts;
		for (const auto part :
			{ parts.Track, parts.Fill, parts.Thumb, parts.Mark, parts.Mixed, parts.Label, parts.Decrement, parts.Increment })
		{
			// A slider's value label may be anywhere (it only displays the value).
			const bool anywhere = part == parts.Label && c.Kind == UiControlKind::Slider;
			if (part && (!Nodes.contains(part.Value) || (!anywhere && !isDescendant(part, node.Id)) || part == node.Id))
			{
				throw std::invalid_argument("UI control parts must be descendants of the control");
			}
		}
		const auto directChild = [&](UiNodeId part)
		{
			return !part || Get(part).Parent == node.Id;
		};
		if (c.Kind == UiControlKind::Slider && (!directChild(parts.Track) || !directChild(parts.Fill) || !directChild(parts.Thumb)))
		{
			throw std::invalid_argument("Slider track, fill and thumb must be children of the slider");
		}
		if (c.Kind == UiControlKind::ScrollBar)
		{
			if (!directChild(parts.Thumb) || !directChild(parts.Decrement) || !directChild(parts.Increment))
			{
				throw std::invalid_argument("A scroll bar's thumb and step buttons must be its children");
			}
			if (c.ScrollTarget &&
				(!Nodes.contains(c.ScrollTarget.Value) || c.ScrollTarget == node.Id || isDescendant(c.ScrollTarget, node.Id)))
			{
				throw std::invalid_argument("A scroll bar's target must be another node outside the bar");
			}
		}
		if (c.Kind == UiControlKind::Toggle && parts.Thumb && (!parts.Track || Get(parts.Thumb).Parent != parts.Track))
		{
			throw std::invalid_argument("A toggle's knob (Thumb) must be a child of its Track");
		}
	}

	void UiDocument::Impl::MarkControlDirty(Node& control)
	{
		Dirty = true; // Part geometry is arrangement only.
		MarkSubtreeVisualDirty(control.Id);
	}

	void UiDocument::Impl::SyncValueLabel(Node& control)
	{
		const auto& c = control.Control;
		if (c.Kind != UiControlKind::Slider || c.LabelDecimals < 0 || !c.Parts.Label || !Nodes.contains(c.Parts.Label.Value))
		{
			return;
		}
		auto& label = Get(c.Parts.Label);
		if (!label.Fonts)
		{
			return;
		}
		char buffer[64];
		const float shown = std::abs(c.Value) < 0.5f * std::pow(10.0f, -float(c.LabelDecimals)) ? 0.0f : c.Value; // No "-0".
		std::snprintf(buffer, sizeof(buffer), "%.*f", int(c.LabelDecimals), double(shown));
		if (label.TextContents != buffer)
		{
			label.TextContents = buffer;
			label.TextLayout.reset();
			label.MeasureLayout.reset();
			label.Selection = ClampSelection(label, label.Selection);
			MarkLayoutDirty(label.Id);
		}
	}

	std::pair<float, float> UiDocument::Impl::ScrollTrack(const Node& bar, const UiRect& inner) const
	{
		const auto& c = bar.Control;
		const bool horizontal = Horizontal(c);
		const float cross = horizontal ? inner.Height : inner.Width;
		const auto button = [&](UiNodeId id)
		{
			return id && Nodes.contains(id.Value) && Get(id).Style.Visible && Get(id).Parent == bar.Id ? cross : 0.0f;
		};
		const float length = Length(inner, horizontal);
		const float start = std::min(button(c.Parts.Decrement), length);
		const float end = std::min(button(c.Parts.Increment), length - start);
		return { start, length - start - end };
	}

	void UiDocument::Impl::StepScrollBar(Node& bar, float direction)
	{
		const auto& c = bar.Control;
		const float step = c.Step > 0.0f ? c.Step : ScrollBarLineStep;
		ChangeValue(bar, c.Value + direction * step, true);
	}

	std::optional<UiRect> UiDocument::Impl::PartGeometry(const Node& part, const UiRect& inner) const
	{
		if (!part.PartOf || !Nodes.contains(part.PartOf.Value))
		{
			return std::nullopt;
		}
		const auto& owner = Get(part.PartOf);
		const auto& c = owner.Control;
		const bool horizontal = Horizontal(c);
		const auto partSize = [&](UiNodeId id)
		{
			return id && Nodes.contains(id.Value) ? Get(id).Desired : UiPoint{};
		};
		if (c.Kind == UiControlKind::Slider && part.Parent == owner.Id)
		{
			const UiPoint thumb = partSize(c.Parts.Thumb);
			const float range = c.Max - c.Min;
			const float t = range > 0.0f ? (c.Value - c.Min) / range : 0.0f;
			const float tick = range > 0.0f ? std::clamp((part.PartValue - c.Min) / range, 0.0f, 1.0f) : 0.0f;
			if (horizontal)
			{
				const float travel = std::max(0.0f, inner.Width - thumb.X);
				const float start = t * travel;
				const float center = start + thumb.X * 0.5f;
				const float thickness = part.Desired.Y;
				switch (part.Role)
				{
				case UiPartRole::Tick:
					return UiRect{ tick * travel + (thumb.X - part.Desired.X) * 0.5f, (inner.Height - thickness) * 0.5f, part.Desired.X,
						thickness };
				case UiPartRole::Track:
					return UiRect{ 0.0f, (inner.Height - thickness) * 0.5f, inner.Width, thickness };
				case UiPartRole::Fill:
					return UiRect{ 0.0f, (inner.Height - thickness) * 0.5f, center, thickness };
				case UiPartRole::Thumb:
					return UiRect{ start, (inner.Height - thumb.Y) * 0.5f, thumb.X, thumb.Y };
				default:
					return std::nullopt;
				}
			}
			const float travel = std::max(0.0f, inner.Height - thumb.Y);
			const float start = (1.0f - t) * travel; // Min at the bottom.
			const float center = start + thumb.Y * 0.5f;
			const float thickness = part.Desired.X;
			switch (part.Role)
			{
			case UiPartRole::Tick:
				return UiRect{ (inner.Width - thickness) * 0.5f, (1.0f - tick) * travel + (thumb.Y - part.Desired.Y) * 0.5f, thickness,
					part.Desired.Y };
			case UiPartRole::Track:
				return UiRect{ (inner.Width - thickness) * 0.5f, 0.0f, thickness, inner.Height };
			case UiPartRole::Fill:
				return UiRect{ (inner.Width - thickness) * 0.5f, center, thickness, std::max(0.0f, inner.Height - center) };
			case UiPartRole::Thumb:
				return UiRect{ (inner.Width - thumb.X) * 0.5f, start, thumb.X, thumb.Y };
			default:
				return std::nullopt;
			}
		}
		if (c.Kind == UiControlKind::ScrollBar && part.Parent == owner.Id &&
			(part.Role == UiPartRole::Decrement || part.Role == UiPartRole::Increment))
		{
			const float cross = horizontal ? inner.Height : inner.Width;
			const float along = part.Role == UiPartRole::Decrement ? 0.0f : std::max(0.0f, Length(inner, horizontal) - cross);
			return horizontal ? UiRect{ along, 0.0f, cross, inner.Height } : UiRect{ 0.0f, along, inner.Width, cross };
		}
		if (c.Kind == UiControlKind::ScrollBar && part.Parent == owner.Id && part.Role == UiPartRole::Thumb)
		{
			const auto [trackStart, track] = ScrollTrack(owner, inner);
			float viewport = track;
			float maximum = 0.0f;
			float offset = 0.0f;
			if (c.ScrollTarget && Nodes.contains(c.ScrollTarget.Value))
			{
				const auto& target = Get(c.ScrollTarget);
				viewport = Length(Internal::ContentBox(target.Bounds, target.Style.Padding), horizontal);
				maximum = horizontal ? target.MaxScroll.X : target.MaxScroll.Y;
				offset = horizontal ? target.Scroll.X : target.Scroll.Y;
			}
			float length = track;
			if (maximum > 0.0f && viewport + maximum > 0.0f)
			{
				length = std::clamp(track * viewport / (viewport + maximum), std::min(c.MinThumbLength, track), track);
			}
			const float position = trackStart + (maximum > 0.0f ? std::clamp(offset / maximum, 0.0f, 1.0f) * (track - length) : 0.0f);
			return horizontal ? UiRect{ position, 0.0f, length, inner.Height } : UiRect{ 0.0f, position, inner.Width, length };
		}
		if (c.Kind == UiControlKind::Toggle && part.Role == UiPartRole::Thumb && part.Parent == c.Parts.Track)
		{
			const UiPoint knob = part.Desired;
			if (horizontal)
			{
				return UiRect{ owner.Knob * std::max(0.0f, inner.Width - knob.X), (inner.Height - knob.Y) * 0.5f, knob.X, knob.Y };
			}
			return UiRect{ (inner.Width - knob.X) * 0.5f, (1.0f - owner.Knob) * std::max(0.0f, inner.Height - knob.Y), knob.X, knob.Y };
		}
		return std::nullopt;
	}

	void UiDocument::Impl::ReArrange(Node& node, UiRect bounds)
	{
		const auto begin = std::find(Order.begin(), Order.end(), node.Id);
		if (begin == Order.end() || !node.Parent)
		{
			return;
		}
		const auto isInside = [&](UiNodeId id)
		{
			for (auto current = Get(id).Parent; current; current = Get(current).Parent)
			{
				if (current == node.Id)
				{
					return true;
				}
			}
			return false;
		};
		auto end = std::next(begin);
		while (end != Order.end() && isInside(*end))
		{
			++end;
		}
		std::vector<UiNodeId> tail(end, Order.end());
		Order.erase(begin, Order.end());
		const auto& parent = Get(node.Parent);
		const UiRect parentInner = Internal::ContentBox(parent.Bounds, parent.Style.Padding);
		const UiRect clip = parent.Style.Clip ? Internal::Intersect(parent.Clip, parentInner) : parent.Clip;
		Arrange(node, bounds, clip, parent.Active);
		Order.insert(Order.end(), tail.begin(), tail.end());
	}

	void UiDocument::Impl::SyncScrollBars()
	{
		// Arrange visits bars and targets in document order, so a bar placed before its
		// target saw last frame's extent. Refresh every bar now that all targets are final.
		const std::vector<UiNodeId> order = Order;
		for (const auto id : order)
		{
			auto& bar = Get(id);
			if (bar.Control.Kind != UiControlKind::ScrollBar || !bar.Active)
			{
				continue;
			}
			auto& c = bar.Control;
			const bool horizontal = Horizontal(c);
			float maximum = 0.0f;
			float offset = 0.0f;
			if (c.ScrollTarget && Nodes.contains(c.ScrollTarget.Value))
			{
				const auto& target = Get(c.ScrollTarget);
				maximum = horizontal ? target.MaxScroll.X : target.MaxScroll.Y;
				offset = horizontal ? target.Scroll.X : target.Scroll.Y;
			}
			if (offset != c.Value || maximum != c.Max)
			{
				bar.ScrollActivity = 0.0f; // Overlay bars reappear while scrolling.
			}
			c.Min = 0.0f;
			c.Max = maximum;
			c.Value = offset;
			const bool hidden = c.Visibility != UiScrollBarVisibility::Always && maximum <= 0.0f;
			if (hidden != bar.ControlHidden)
			{
				bar.ControlHidden = hidden;
				MarkSubtreeVisualDirty(bar.Id);
			}
			if (c.Parts.Thumb && Nodes.contains(c.Parts.Thumb.Value))
			{
				auto& thumb = Get(c.Parts.Thumb);
				if (thumb.Parent != bar.Id || !thumb.Style.Visible)
				{
					continue;
				}
				const UiRect inner = Internal::ContentBox(bar.Bounds, bar.Style.Padding);
				if (const auto rect = PartGeometry(thumb, inner))
				{
					const UiRect placed{ rect->X + inner.X - bar.Scroll.X, rect->Y + inner.Y - bar.Scroll.Y, rect->Width, rect->Height };
					if (!Internal::SameRect(placed, thumb.Bounds))
					{
						ReArrange(thumb, placed);
					}
				}
			}
		}
	}

	float UiDocument::Impl::Axis(const Node& control, UiPoint logical) const
	{
		return Horizontal(control.Control) ? logical.X : logical.Y;
	}

	bool UiDocument::Impl::ChangeValue(Node& node, float value, bool commit)
	{
		auto& c = node.Control;
		value = ClampValue(c, value);
		const bool changed = value != c.Value;
		if (changed)
		{
			c.Value = value;
			if (c.Kind == UiControlKind::ScrollBar && c.ScrollTarget && Nodes.contains(c.ScrollTarget.Value))
			{
				auto& target = Get(c.ScrollTarget);
				(Horizontal(c) ? target.Scroll.X : target.Scroll.Y) = value;
				node.ScrollActivity = 0.0f;
			}
			MarkControlDirty(node);
			SyncValueLabel(node);
			Events.push_back({ UiEventKind::ValueChanged, node.Id, value });
		}
		if (commit && changed)
		{
			Events.push_back({ UiEventKind::ValueCommitted, node.Id, value });
		}
		return changed;
	}

	void UiDocument::Impl::Toggle(Node& node)
	{
		auto& c = node.Control;
		if (c.ReadOnly || (c.Kind != UiControlKind::Checkbox && c.Kind != UiControlKind::Toggle))
		{
			return;
		}
		c.Check = c.Check == UiCheckState::Checked ? UiCheckState::Unchecked : UiCheckState::Checked;
		MarkControlDirty(node);
		const float value = CheckValue(c.Check);
		Events.push_back({ UiEventKind::ValueChanged, node.Id, value });
		Events.push_back({ UiEventKind::ValueCommitted, node.Id, value });
	}

	bool UiDocument::Impl::ControlPointerDown(Node& node, UiPoint logical)
	{
		auto& c = node.Control;
		const bool horizontal = Horizontal(c);
		const float axis = Axis(node, logical);
		DragMoved = false;
		DragStart = axis;
		PressValue = c.Kind == UiControlKind::Checkbox || c.Kind == UiControlKind::Toggle ? CheckValue(c.Check) : c.Value;
		if (c.ReadOnly)
		{
			return false;
		}
		const auto thumbBounds = [&]() -> std::optional<UiRect>
		{
			if (c.Parts.Thumb && Nodes.contains(c.Parts.Thumb.Value) && Get(c.Parts.Thumb).Active)
			{
				return Get(c.Parts.Thumb).Bounds;
			}
			return std::nullopt;
		};
		if (c.Kind == UiControlKind::ScrollBar)
		{
			// Step buttons: one step now, repeats while held (Update).
			for (const auto& [button, direction] : { std::pair{ c.Parts.Decrement, -1.0f }, std::pair{ c.Parts.Increment, 1.0f } })
			{
				if (button && Nodes.contains(button.Value) && Get(button).Active && Get(button).Bounds.Contains(logical))
				{
					StepScrollBar(node, direction);
					Stepping = node.Id;
					StepDirection = direction;
					StepHeld = 0.0f;
					NextStep = StepRepeatDelay;
					return true;
				}
			}
		}
		switch (c.Kind)
		{
		case UiControlKind::Slider:
		case UiControlKind::ScrollBar:
		{
			const auto thumb = thumbBounds();
			const float thumbStart = thumb ? Along(*thumb, horizontal) : axis;
			const float thumbLength = thumb ? Length(*thumb, horizontal) : 0.0f;
			const bool onThumb = thumb && axis >= thumbStart && axis < thumbStart + thumbLength;
			if (onThumb)
			{
				DragGrab = axis - thumbStart;
				Dragging = node.Id;
				return true;
			}
			if (c.TrackClick == UiTrackClick::Page)
			{
				const float page = c.PageStep > 0.0f ? c.PageStep
					: c.Kind == UiControlKind::ScrollBar && c.ScrollTarget && Nodes.contains(c.ScrollTarget.Value)
					? Length(Internal::ContentBox(Get(c.ScrollTarget).Bounds, Get(c.ScrollTarget).Style.Padding), horizontal)
					: (c.Max - c.Min) * 0.1f;
				// Towards the pointer; vertical sliders grow upwards.
				bool increase = axis >= thumbStart + thumbLength * 0.5f;
				if (c.Kind == UiControlKind::Slider && !horizontal)
				{
					increase = !increase;
				}
				ChangeValue(node, c.Value + (increase ? page : -page), true);
				return true;
			}
			DragGrab = thumbLength * 0.5f;
			Dragging = node.Id;
			DragMoved = true;
			ControlPointerMove(logical);
			return true;
		}
		case UiControlKind::Toggle:
		{
			const auto knob = thumbBounds();
			DragGrab = knob && axis >= Along(*knob, horizontal) && axis < Along(*knob, horizontal) + Length(*knob, horizontal)
				? axis - Along(*knob, horizontal)
				: (knob ? Length(*knob, horizontal) * 0.5f : 0.0f);
			Dragging = node.Id;
			return true;
		}
		default:
			return false;
		}
	}

	void UiDocument::Impl::ControlPointerMove(UiPoint logical)
	{
		if (!Dragging || Dragging != Pressed || !Nodes.contains(Dragging.Value))
		{
			return;
		}
		auto& node = Get(Dragging);
		auto& c = node.Control;
		const bool horizontal = Horizontal(c);
		const float axis = Axis(node, logical);
		if (std::abs(axis - DragStart) > KnobDragThreshold)
		{
			DragMoved = true;
		}
		switch (c.Kind)
		{
		case UiControlKind::Slider:
		{
			const UiRect inner = Internal::ContentBox(node.Bounds, node.Style.Padding);
			const UiPoint thumb = c.Parts.Thumb && Nodes.contains(c.Parts.Thumb.Value) ? Get(c.Parts.Thumb).Desired : UiPoint{};
			const float travel = std::max(0.0f, Length(inner, horizontal) - Length(thumb, horizontal));
			float t = travel > 0.0f ? (axis - DragGrab - Along(inner, horizontal)) / travel : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			if (!horizontal)
			{
				t = 1.0f - t;
			}
			ChangeValue(node, c.Min + t * (c.Max - c.Min), false);
			break;
		}
		case UiControlKind::ScrollBar:
		{
			if (!c.Parts.Thumb || !Nodes.contains(c.Parts.Thumb.Value))
			{
				break;
			}
			const UiRect inner = Internal::ContentBox(node.Bounds, node.Style.Padding);
			const auto [trackStart, trackLength] = ScrollTrack(node, inner);
			const float thumbLength = Length(Get(c.Parts.Thumb).Bounds, horizontal);
			const float travel = std::max(0.0f, trackLength - thumbLength);
			const float t =
				travel > 0.0f ? std::clamp((axis - DragGrab - Along(inner, horizontal) - trackStart) / travel, 0.0f, 1.0f) : 0.0f;
			ChangeValue(node, t * c.Max, false);
			break;
		}
		case UiControlKind::Toggle:
		{
			if (!DragMoved || !c.Parts.Track || !Nodes.contains(c.Parts.Track.Value) || !c.Parts.Thumb ||
				!Nodes.contains(c.Parts.Thumb.Value))
			{
				break;
			}
			const auto& track = Get(c.Parts.Track);
			const UiRect inner = Internal::ContentBox(track.Bounds, track.Style.Padding);
			const float knob = Length(Get(c.Parts.Thumb).Desired, horizontal);
			const float travel = std::max(0.0f, Length(inner, horizontal) - knob);
			float t = travel > 0.0f ? std::clamp((axis - DragGrab - Along(inner, horizontal)) / travel, 0.0f, 1.0f) : node.Knob;
			if (!horizontal)
			{
				t = 1.0f - t;
			}
			if (t != node.Knob)
			{
				node.Knob = t;
				Dirty = true;
			}
			break;
		}
		default:
			break;
		}
	}

	void UiDocument::Impl::EndDrag(bool commit)
	{
		if (!Dragging || !Nodes.contains(Dragging.Value))
		{
			Dragging = {};
			return;
		}
		auto& node = Get(Dragging);
		Dragging = {};
		MarkSubtreeVisualDirty(node.Id);
		auto& c = node.Control;
		if (c.Kind == UiControlKind::Toggle)
		{
			if (DragMoved && commit && !c.ReadOnly)
			{
				const auto state = node.Knob >= 0.5f ? UiCheckState::Checked : UiCheckState::Unchecked;
				if (state != c.Check)
				{
					c.Check = state;
					const float value = CheckValue(state);
					Events.push_back({ UiEventKind::ValueChanged, node.Id, value });
					Events.push_back({ UiEventKind::ValueCommitted, node.Id, value });
				}
				MarkControlDirty(node); // The knob eases (or snaps) to the final state.
			}
			return;
		}
		if (commit && c.Value != PressValue)
		{
			Events.push_back({ UiEventKind::ValueCommitted, node.Id, c.Value });
		}
	}

	void UiDocument::Impl::ControlPointerUp(Node& node, bool inside)
	{
		if (Stepping == node.Id)
		{
			Stepping = {};
		}
		const bool dragged = Dragging == node.Id;
		const bool moved = DragMoved;
		if (dragged)
		{
			EndDrag(true);
		}
		const auto kind = node.Control.Kind;
		if (inside && (kind == UiControlKind::Checkbox || (kind == UiControlKind::Toggle && !(dragged && moved))))
		{
			Toggle(node);
		}
	}

	bool UiDocument::Impl::ControlKey(Node& node, UiKey key)
	{
		auto& c = node.Control;
		if (c.ReadOnly || (c.Kind != UiControlKind::Slider && c.Kind != UiControlKind::ScrollBar))
		{
			return false;
		}
		const bool horizontal = Horizontal(c);
		const bool slider = c.Kind == UiControlKind::Slider;
		const float range = c.Max - c.Min;
		const float step = c.Step > 0.0f ? c.Step : slider ? range * 0.01f : ScrollBarLineStep;
		float page = c.PageStep;
		if (page <= 0.0f)
		{
			page = slider ? range * 0.1f
				: c.ScrollTarget && Nodes.contains(c.ScrollTarget.Value)
				? Length(Internal::ContentBox(Get(c.ScrollTarget).Bounds, Get(c.ScrollTarget).Style.Padding), horizontal)
				: ScrollBarLineStep;
		}
		// Sliders grow right and up; scroll bars scroll towards larger offsets right and down.
		float delta = 0.0f;
		switch (key)
		{
		case UiKey::Left:
			delta = horizontal ? -step : 0.0f;
			break;
		case UiKey::Right:
			delta = horizontal ? step : 0.0f;
			break;
		case UiKey::Up:
			delta = horizontal ? 0.0f : (slider ? step : -step);
			break;
		case UiKey::Down:
			delta = horizontal ? 0.0f : (slider ? -step : step);
			break;
		case UiKey::PageUp:
			delta = slider ? page : -page;
			break;
		case UiKey::PageDown:
			delta = slider ? -page : page;
			break;
		case UiKey::Home:
			ChangeValue(node, c.Min, true);
			return true;
		case UiKey::End:
			ChangeValue(node, c.Max, true);
			return true;
		default:
			return false;
		}
		if (delta == 0.0f)
		{
			return false; // The cross axis navigates.
		}
		ChangeValue(node, c.Value + delta, true);
		return true;
	}

	bool UiDocument::Impl::ControlWheel(Node& node, UiPoint delta)
	{
		auto& c = node.Control;
		if (c.ReadOnly)
		{
			return false;
		}
		if (c.Kind == UiControlKind::ScrollBar)
		{
			const float amount = Horizontal(c) && delta.X != 0.0f ? delta.X : delta.Y;
			return ChangeValue(node, c.Value + amount, true);
		}
		if (c.Kind == UiControlKind::Slider && delta.Y != 0.0f)
		{
			const float step = c.Step > 0.0f ? c.Step : (c.Max - c.Min) * 0.01f;
			return ChangeValue(node, c.Value + (delta.Y < 0.0f ? step : -step), true); // Wheel up increases.
		}
		return false;
	}

	bool UiDocument::Impl::AnimateControls(float seconds)
	{
		bool animating = false;
		if (Stepping && (Stepping != Pressed || !Nodes.contains(Stepping.Value)))
		{
			Stepping = {}; // Released or cancelled.
		}
		if (Stepping)
		{
			StepHeld += seconds;
			while (StepHeld >= NextStep)
			{
				StepScrollBar(Get(Stepping), StepDirection);
				NextStep += StepRepeatInterval;
			}
			animating = true;
		}
		for (auto& [key, node] : Nodes)
		{
			auto& c = node.Control;
			if (c.Kind == UiControlKind::Toggle && !(Dragging == node.Id && DragMoved))
			{
				const float target = c.Check == UiCheckState::Checked ? 1.0f : 0.0f;
				if (node.Knob != target)
				{
					float duration = node.Style.TransitionSeconds;
					if (c.Parts.Thumb && Nodes.contains(c.Parts.Thumb.Value))
					{
						duration = Get(c.Parts.Thumb).Style.TransitionSeconds;
					}
					const float stepSize = duration > MinimumKnobSeconds ? seconds / duration : 1.0f;
					node.Knob = node.Knob < target ? std::min(target, node.Knob + stepSize) : std::max(target, node.Knob - stepSize);
					Dirty = true;
					animating = animating || node.Knob != target;
				}
			}
			if (c.Kind == UiControlKind::ScrollBar && c.Visibility == UiScrollBarVisibility::Overlay)
			{
				const bool held = Hover == node.Id || Dragging == node.Id;
				node.ScrollActivity = held ? 0.0f : node.ScrollActivity + seconds;
				float opacity = 1.0f;
				if (node.ScrollActivity > c.FadeDelaySeconds)
				{
					opacity =
						c.FadeSeconds > 0.0f ? std::max(0.0f, 1.0f - (node.ScrollActivity - c.FadeDelaySeconds) / c.FadeSeconds) : 0.0f;
				}
				if (opacity != node.ControlOpacity)
				{
					node.ControlOpacity = opacity;
					node.PaintDirty = true;
				}
				animating = animating || (!node.ControlHidden && opacity > 0.0f);
			}
			else
			{
				node.ControlOpacity = 1.0f;
			}
		}
		return animating;
	}

	void UiDocument::SetControl(UiNodeId id, const UiControl& control)
	{
		auto& node = impl->Get(id);
		impl->ValidateControl(node, control);
		const auto& old = node.Control.Parts;
		for (const auto part : { old.Track, old.Fill, old.Thumb, old.Mark, old.Mixed, old.Label, old.Decrement, old.Increment })
		{
			if (part && impl->Nodes.contains(part.Value) && impl->Get(part).PartOf == id)
			{
				auto& p = impl->Get(part);
				p.PartOf = {};
				p.Role = UiPartRole::None;
				impl->MarkLayoutDirty(part);
			}
		}
		if (impl->Dragging == id)
		{
			impl->Dragging = {};
		}
		if (impl->Stepping == id)
		{
			impl->Stepping = {};
		}
		node.Control = control;
		node.Control.Value = impl->ClampValue(control, control.Value);
		node.Knob = control.Check == UiCheckState::Checked ? 1.0f : 0.0f;
		node.ControlHidden = false;
		node.ControlOpacity = 1.0f;
		node.ScrollActivity = 0.0f;
		const std::pair<UiNodeId, UiPartRole> roles[] = { { control.Parts.Track, UiPartRole::Track },
			{ control.Parts.Fill, UiPartRole::Fill }, { control.Parts.Thumb, UiPartRole::Thumb }, { control.Parts.Mark, UiPartRole::Mark },
			{ control.Parts.Mixed, UiPartRole::Mixed }, { control.Parts.Label, UiPartRole::Label },
			{ control.Parts.Decrement, UiPartRole::Decrement }, { control.Parts.Increment, UiPartRole::Increment } };
		for (const auto& [part, role] : roles)
		{
			if (part)
			{
				auto& p = impl->Get(part);
				p.PartOf = id;
				p.Role = role;
				impl->MarkLayoutDirty(part);
			}
		}
		// A themed control's axes follow its orientation.
		for (const auto& [part, role] : roles)
		{
			if (part && impl->Get(part).ThemeClass != UiThemeClass::None)
			{
				impl->ApplyTheme(impl->Get(part));
			}
		}
		if (node.ThemeClass != UiThemeClass::None)
		{
			impl->ApplyTheme(node);
		}
		impl->SyncValueLabel(node);
		impl->MarkLayoutDirty(id);
		impl->MarkSubtreeVisualDirty(id);
		impl->ClearUnavailable();
	}

	void UiDocument::SetPartRole(UiNodeId partId, UiNodeId controlId, UiPartRole role, float value)
	{
		auto& part = impl->Get(partId);
		if (role == UiPartRole::None)
		{
			part.PartOf = {};
			part.Role = UiPartRole::None;
			impl->MarkLayoutDirty(partId);
			return;
		}
		const auto& control = impl->Get(controlId);
		if (role != UiPartRole::Tick || control.Control.Kind != UiControlKind::Slider || part.Parent != controlId || !std::isfinite(value))
		{
			throw std::invalid_argument("SetPartRole registers slider tick marks (direct children) at finite values");
		}
		part.PartOf = controlId;
		part.Role = role;
		part.PartValue = value;
		if (part.ThemeClass != UiThemeClass::None)
		{
			impl->ApplyTheme(part);
		}
		impl->MarkLayoutDirty(partId);
	}

	const UiControl& UiDocument::GetControl(UiNodeId id) const
	{
		return impl->Get(id).Control;
	}

	void UiDocument::SetValue(UiNodeId id, float value)
	{
		auto& node = impl->Get(id);
		auto& c = node.Control;
		switch (c.Kind)
		{
		case UiControlKind::Slider:
			value = impl->ClampValue(c, value);
			if (value != c.Value)
			{
				c.Value = value;
				impl->MarkControlDirty(node);
				impl->SyncValueLabel(node);
			}
			break;
		case UiControlKind::ScrollBar:
			if (c.ScrollTarget && impl->Nodes.contains(c.ScrollTarget.Value))
			{
				auto scroll = GetScroll(c.ScrollTarget);
				(c.Orientation == UiOrientation::Horizontal ? scroll.X : scroll.Y) = std::isfinite(value) ? value : 0.0f;
				SetScroll(c.ScrollTarget, scroll); // Clamped by the next Layout, which syncs the bar.
			}
			break;
		case UiControlKind::Checkbox:
		case UiControlKind::Toggle:
			SetChecked(id, value > 0.5f ? UiCheckState::Checked : UiCheckState::Unchecked);
			break;
		default:
			throw std::invalid_argument("SetValue needs a slider, scroll bar, checkbox or toggle");
		}
	}

	float UiDocument::GetValue(UiNodeId id) const
	{
		const auto& node = impl->Get(id);
		const auto& c = node.Control;
		if (c.Kind == UiControlKind::Checkbox || c.Kind == UiControlKind::Toggle)
		{
			return impl->CheckValue(c.Check);
		}
		if (c.Kind == UiControlKind::ScrollBar && c.ScrollTarget && impl->Nodes.contains(c.ScrollTarget.Value))
		{
			const auto scroll = impl->Get(c.ScrollTarget).Scroll;
			return c.Orientation == UiOrientation::Horizontal ? scroll.X : scroll.Y;
		}
		return c.Value;
	}

	void UiDocument::SetChecked(UiNodeId id, UiCheckState state)
	{
		auto& node = impl->Get(id);
		auto& c = node.Control;
		if (c.Kind != UiControlKind::Checkbox && c.Kind != UiControlKind::Toggle)
		{
			throw std::invalid_argument("SetChecked needs a checkbox or toggle");
		}
		if (static_cast<std::uint8_t>(state) > static_cast<std::uint8_t>(UiCheckState::Mixed) ||
			(c.Kind == UiControlKind::Toggle && state == UiCheckState::Mixed))
		{
			throw std::invalid_argument("Invalid check state for this control");
		}
		if (state != c.Check)
		{
			c.Check = state;
			if (impl->Dragging != id)
			{
				node.Knob = state == UiCheckState::Checked ? 1.0f : 0.0f; // From code: no easing.
			}
			impl->MarkControlDirty(node);
		}
	}

	UiCheckState UiDocument::GetChecked(UiNodeId id) const
	{
		return impl->Get(id).Control.Check;
	}
} // namespace Swim::UI
