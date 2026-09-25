#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"
#include "Engine/Systems/Text/Utf8.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Swim::UI
{
	namespace
	{
		bool Finite(UiPoint point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y);
		}
	} // namespace

	UiNodeId UiDocument::HitTest(UiPoint framebufferPoint) const
	{
		impl->RequireLayout();
		if (!Finite(framebufferPoint))
		{
			return {};
		}
		const UiPoint point{ framebufferPoint.X / impl->Dpi, framebufferPoint.Y / impl->Dpi };
		for (auto it = impl->Order.rbegin(); it != impl->Order.rend(); ++it)
		{
			const auto& node = impl->Get(*it);
			if (node.Active && impl->IsHitTestable(node) && node.Bounds.Contains(point) && node.Clip.Contains(point))
			{
				return node.Id;
			}
		}
		return {};
	}

	void UiDocument::EnsureLayout()
	{
		if (impl->Dirty && impl->Revision > 0)
		{
			Layout(impl->Framebuffer, impl->Dpi);
		}
	}

	void UiDocument::PointerMove(UiPoint point)
	{
		EnsureLayout();
		const auto hit = HitTest(point);
		if (hit != impl->Hover)
		{
			if (impl->Hover)
			{
				impl->Events.push_back({ UiEventKind::Leave, impl->Hover });
			}
			impl->Hover = hit;
			if (hit)
			{
				impl->Events.push_back({ UiEventKind::Enter, hit });
			}
		}
		// Drag selection and control drags continue outside the node's bounds.
		if (impl->Selecting && impl->Selecting == impl->Pressed && Finite(point))
		{
			auto& node = impl->Get(impl->Selecting);
			impl->SetCaret(node, impl->TextHit(node, { point.X / impl->Dpi, point.Y / impl->Dpi }), true);
		}
		if (impl->Dragging && Finite(point))
		{
			impl->ControlPointerMove({ point.X / impl->Dpi, point.Y / impl->Dpi });
		}
	}

	void UiDocument::PointerLeave()
	{
		if (impl->Hover)
		{
			impl->Events.push_back({ UiEventKind::Leave, impl->Hover });
			impl->Hover = {};
		}
	}

	void UiDocument::PointerDown(UiPoint point, UiKeyModifiers modifiers)
	{
		PointerMove(point);
		CancelPointer();
		impl->Pressed = impl->Hover;
		Focus(impl->Pressed && impl->IsFocusable(impl->Get(impl->Pressed)) ? impl->Pressed : UiNodeId{});
		if (impl->Pressed)
		{
			impl->Events.push_back({ UiEventKind::Press, impl->Pressed });
			auto& node = impl->Get(impl->Pressed);
			if (node.Editable && node.Fonts)
			{
				impl->EndComposition();
				impl->SetCaret(node, impl->TextHit(node, { point.X / impl->Dpi, point.Y / impl->Dpi }), modifiers.Shift);
				impl->Selecting = node.Id;
			}
			if (impl->IsControl(node) && Finite(point))
			{
				impl->ControlPointerDown(node, { point.X / impl->Dpi, point.Y / impl->Dpi });
			}
		}
	}

	void UiDocument::PointerUp(UiPoint point)
	{
		PointerMove(point);
		impl->Selecting = {};
		if (impl->Pressed)
		{
			if (impl->IsControl(impl->Get(impl->Pressed)))
			{
				impl->ControlPointerUp(impl->Get(impl->Pressed), impl->Pressed == impl->Hover);
			}
			impl->Events.push_back({ UiEventKind::Release, impl->Pressed });
			if (impl->Pressed == impl->Hover)
			{
				impl->Events.push_back({ UiEventKind::Click, impl->Pressed });
			}
			impl->Pressed = {};
		}
	}

	void UiDocument::CancelPointer()
	{
		impl->Selecting = {};
		if (impl->Dragging)
		{
			impl->EndDrag(true); // Keeps (and commits) the value reached so far.
		}
		if (impl->Pressed)
		{
			impl->Events.push_back({ UiEventKind::Cancel, impl->Pressed });
			impl->Pressed = {};
		}
	}

	bool UiDocument::Wheel(UiPoint framebufferPoint, UiPoint delta)
	{
		EnsureLayout();
		impl->RequireLayout();
		if (!Finite(framebufferPoint) || !Finite(delta))
		{
			return false;
		}
		const UiPoint point{ framebufferPoint.X / impl->Dpi, framebufferPoint.Y / impl->Dpi };
		// Scroll bars under the pointer scroll their target; a focused slider under the
		// pointer steps its value. Anything else scrolls the innermost clipped node.
		if (const auto hit = HitTest(framebufferPoint))
		{
			auto& node = impl->Get(hit);
			const auto kind = node.Control.Kind;
			if (kind == UiControlKind::ScrollBar || (kind == UiControlKind::Slider && impl->Focused == hit))
			{
				return impl->ControlWheel(node, delta);
			}
		}
		for (auto it = impl->Order.rbegin(); it != impl->Order.rend(); ++it)
		{
			auto& node = impl->Get(*it);
			if (!node.Active || !node.Style.Clip || !node.Bounds.Contains(point) || !node.Clip.Contains(point))
			{
				continue;
			}
			const UiPoint target{ std::clamp(node.Scroll.X + delta.X, 0.0f, node.MaxScroll.X),
				std::clamp(node.Scroll.Y + delta.Y, 0.0f, node.MaxScroll.Y) };
			if (target.X != node.Scroll.X || target.Y != node.Scroll.Y)
			{
				node.Scroll = target;
				impl->Dirty = true;
				return true;
			}
		}
		return false;
	}

	void UiDocument::Focus(UiNodeId id)
	{
		if (id && (!impl->Available(id) || !impl->IsFocusable(impl->Get(id))))
		{
			throw std::invalid_argument("UI focus target is unavailable");
		}
		if (id == impl->Focused)
		{
			return;
		}
		impl->Composition.clear();
		if (impl->Focused)
		{
			impl->Events.push_back({ UiEventKind::Blur, impl->Focused });
			impl->MarkPaintDirty(impl->Focused);
			if (impl->Get(impl->Focused).Editable)
			{
				impl->MarkLayoutDirty(impl->Focused); // Drops any displayed preedit text.
			}
		}
		impl->Focused = id;
		if (id)
		{
			impl->Events.push_back({ UiEventKind::Focus, id });
			impl->MarkPaintDirty(id);
			impl->Get(id).RevealCaret = impl->Get(id).Editable;
		}
	}

	std::vector<UiNodeId> UiDocument::Impl::TabOrder() const
	{
		std::vector<UiNodeId> candidates;
		for (auto id : Order)
		{
			const auto& node = Get(id);
			if (node.Active && IsFocusable(node) && node.Style.TabIndex >= 0)
			{
				candidates.push_back(id);
			}
		}
		// Positive indices first in ascending order, then 0 in document order.
		std::stable_sort(candidates.begin(), candidates.end(),
			[&](UiNodeId a, UiNodeId b)
			{
				const auto ia = Get(a).Style.TabIndex;
				const auto ib = Get(b).Style.TabIndex;
				if ((ia > 0) != (ib > 0))
				{
					return ia > 0;
				}
				return ia > 0 && ia < ib;
			});
		return candidates;
	}

	void UiDocument::FocusNext(bool backwards)
	{
		EnsureLayout();
		impl->RequireLayout();
		const auto candidates = impl->TabOrder();
		if (candidates.empty())
		{
			Focus({});
			return;
		}
		const auto current = std::find(candidates.begin(), candidates.end(), impl->Focused);
		const auto index = static_cast<std::size_t>(current - candidates.begin());
		const auto next = current == candidates.end()
			? (backwards ? candidates.size() - 1 : 0)
			: (backwards ? (index + candidates.size() - 1) % candidates.size() : (index + 1) % candidates.size());
		Focus(candidates[next]);
	}

	bool UiDocument::Navigate(UiNavDirection direction)
	{
		EnsureLayout();
		impl->RequireLayout();
		const auto candidates = impl->TabOrder();
		if (candidates.empty())
		{
			return false;
		}
		if (!impl->Focused || std::find(candidates.begin(), candidates.end(), impl->Focused) == candidates.end())
		{
			Focus(candidates.front());
			return true;
		}
		const auto from = impl->Get(impl->Focused).Bounds;
		const bool horizontal = direction == UiNavDirection::Left || direction == UiNavDirection::Right;
		const float sign = direction == UiNavDirection::Right || direction == UiNavDirection::Down ? 1.0f : -1.0f;
		UiNodeId best;
		float bestScore = std::numeric_limits<float>::infinity();
		for (const auto id : candidates)
		{
			if (id == impl->Focused)
			{
				continue;
			}
			const auto to = impl->Get(id).Bounds;
			// Distance along the direction between facing edges (centers break ties for
			// overlapping boxes), and the gap across it (0 when the boxes overlap).
			const float fromLow = horizontal ? from.X : from.Y;
			const float fromHigh = fromLow + (horizontal ? from.Width : from.Height);
			const float toLow = horizontal ? to.X : to.Y;
			const float toHigh = toLow + (horizontal ? to.Width : to.Height);
			const float fromCenter = (fromLow + fromHigh) * 0.5f;
			const float toCenter = (toLow + toHigh) * 0.5f;
			if ((toCenter - fromCenter) * sign <= 0.5f)
			{
				continue;
			}
			const float primary = std::max(0.0f, sign > 0.0f ? toLow - fromHigh : fromLow - toHigh);
			const float crossLowA = horizontal ? from.Y : from.X;
			const float crossHighA = crossLowA + (horizontal ? from.Height : from.Width);
			const float crossLowB = horizontal ? to.Y : to.X;
			const float crossHighB = crossLowB + (horizontal ? to.Height : to.Width);
			const float cross = std::max({ 0.0f, crossLowB - crossHighA, crossLowA - crossHighB });
			const float score = primary + 2.0f * cross + 0.001f * std::abs(toCenter - fromCenter);
			if (score < bestScore)
			{
				bestScore = score;
				best = id;
			}
		}
		if (!best)
		{
			return false;
		}
		Focus(best);
		return true;
	}

	void UiDocument::SetArrowNavigation(bool enabled)
	{
		impl->ArrowNavigation = enabled;
	}

	void UiDocument::ActivateFocused()
	{
		if (!impl->Focused)
		{
			return;
		}
		auto& node = impl->Get(impl->Focused);
		const auto kind = node.Control.Kind;
		if (kind == UiControlKind::Checkbox || kind == UiControlKind::Toggle)
		{
			impl->Toggle(node);
			return;
		}
		impl->Events.push_back({ UiEventKind::Click, impl->Focused });
	}

	UiNodeId UiDocument::GetFocus() const
	{
		return impl->Focused;
	}

	bool UiDocument::KeyDown(UiKey key, UiKeyModifiers modifiers)
	{
		if (key == UiKey::Tab && !(impl->Focused && impl->Get(impl->Focused).Editable && !impl->Composition.empty()))
		{
			FocusNext(modifiers.Shift);
			return true;
		}
		if (!impl->Focused)
		{
			return false;
		}
		auto& node = impl->Get(impl->Focused);
		if (node.Editable && node.Fonts)
		{
			return impl->EditKey(node, key, modifiers);
		}
		if (impl->IsControl(node) && impl->ControlKey(node, key))
		{
			return true;
		}
		if (key == UiKey::Enter || key == UiKey::Space)
		{
			ActivateFocused();
			return true;
		}
		if (key == UiKey::Escape)
		{
			Focus({});
			return true;
		}
		if (impl->ArrowNavigation)
		{
			switch (key)
			{
			case UiKey::Left:
				return Navigate(UiNavDirection::Left);
			case UiKey::Right:
				return Navigate(UiNavDirection::Right);
			case UiKey::Up:
				return Navigate(UiNavDirection::Up);
			case UiKey::Down:
				return Navigate(UiNavDirection::Down);
			default:
				break;
			}
		}
		return false;
	}

	void UiDocument::TextInput(std::string_view utf8)
	{
		if (!impl->Focused || utf8.empty())
		{
			return;
		}
		auto& node = impl->Get(impl->Focused);
		if (!node.Editable || !node.Fonts)
		{
			return;
		}
		impl->EndComposition();
		impl->ReplaceSelection(node, utf8);
	}

	void UiDocument::SetComposition(std::string_view utf8, std::uint32_t cursor)
	{
		if (!impl->Focused)
		{
			return;
		}
		auto& node = impl->Get(impl->Focused);
		if (!node.Editable || !node.Fonts)
		{
			return;
		}
		auto text = Text::SanitizeUtf8(utf8);
		// Line breaks never enter a single-line field, not even as preedit.
		if (!node.EditOptions.Multiline)
		{
			std::erase_if(text,
				[](char c)
				{
					return c == '\n' || c == '\r';
				});
		}
		if (!text.empty() && node.Selection.Anchor != node.Selection.Caret)
		{
			impl->ReplaceSelection(node, {}); // Composition replaces the selection.
		}
		if (text == impl->Composition && cursor == impl->CompositionCursor)
		{
			return;
		}
		impl->Composition = std::move(text);
		impl->CompositionCursor = std::min<std::uint32_t>(cursor, static_cast<std::uint32_t>(impl->Composition.size()));
		node.RevealCaret = true;
		impl->MarkLayoutDirty(node.Id);
	}

	const std::string& UiDocument::GetComposition() const
	{
		return impl->Composition;
	}

	void UiDocument::SetClipboard(UiClipboard clipboard)
	{
		impl->Clipboard = std::move(clipboard);
	}

	bool UiDocument::WantsTextInput() const
	{
		return impl->Focused && impl->Get(impl->Focused).Editable;
	}

	UiRect UiDocument::GetTextInputRect() const
	{
		impl->RequireLayout();
		if (!WantsTextInput())
		{
			return {};
		}
		const auto& node = impl->Get(impl->Focused);
		if (!node.TextLayout)
		{
			return {};
		}
		const auto offset = impl->IsComposing(node)
			? node.Selection.Caret + std::min<std::uint32_t>(impl->CompositionCursor, static_cast<std::uint32_t>(impl->Composition.size()))
			: node.Selection.Caret;
		const auto caret = node.TextLayout->GetCaret(offset);
		const UiRect content = Internal::ContentBox(node.Bounds, node.Style.Padding);
		const float dpi = impl->Dpi;
		return { (content.X - node.Scroll.X + caret.X) * dpi, (content.Y - node.Scroll.Y + caret.Top) * dpi, std::max(1.0f, dpi),
			caret.Height * dpi };
	}

	void UiDocument::SetCaretVisible(bool visible)
	{
		if (impl->CaretVisible != visible)
		{
			impl->CaretVisible = visible;
			impl->MarkPaintDirty(impl->Focused);
		}
	}
} // namespace Swim::UI
