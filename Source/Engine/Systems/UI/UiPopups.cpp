#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <stdexcept>

// Popups (critical-path item 79): menus, dropdown lists, tooltips and modal dialogs are
// children of the root that the document shows, places after Layout, paints above
// everything else in opening order and dismisses (light dismiss, Escape, activation).
namespace Swim::UI
{
	namespace
	{
		bool Covers(const UiRect& rect, UiPoint point)
		{
			return rect.Width > 0.0f && rect.Height > 0.0f && rect.Contains(point);
		}
	} // namespace

	std::optional<std::size_t> UiDocument::Impl::PopupIndexOf(UiNodeId id) const
	{
		if (!id || !Nodes.contains(id.Value))
		{
			return std::nullopt;
		}
		// The popup is the root child on the node's ancestor chain.
		auto current = id;
		while (Get(current).Parent && Get(current).Parent != Root)
		{
			current = Get(current).Parent;
		}
		for (std::size_t i = 0; i < Popups.size(); ++i)
		{
			if (Popups[i].Node == current)
			{
				return i;
			}
		}
		return std::nullopt;
	}

	std::optional<std::size_t> UiDocument::Impl::TopModal() const
	{
		for (std::size_t i = Popups.size(); i-- > 0;)
		{
			if (Popups[i].Desc.Modal)
			{
				return i;
			}
		}
		return std::nullopt;
	}

	bool UiDocument::Impl::InputAllowed(UiNodeId id) const
	{
		const auto modal = TopModal();
		if (!modal)
		{
			return true;
		}
		const auto index = PopupIndexOf(id);
		return index && *index >= *modal;
	}

	void UiDocument::Impl::ShowPopup(Node& node, bool visible)
	{
		if (node.Style.Visible == visible && node.Style.Absolute)
		{
			return;
		}
		node.Style.Visible = visible;
		node.Style.Absolute = true; // Placed by the document, outside the root's flow.
		MarkLayoutDirty(node.Id);
		MarkLayoutDirty(Root);
	}

	void UiDocument::Impl::ClosePopupsFrom(std::size_t index)
	{
		while (Popups.size() > index)
		{
			const auto entry = Popups.back();
			Popups.pop_back();
			if (!Nodes.contains(entry.Node.Value))
			{
				continue;
			}
			bool focusInside = false;
			for (auto current = Focused; current && Nodes.contains(current.Value); current = Get(current).Parent)
			{
				if (current == entry.Node)
				{
					focusInside = true;
					break;
				}
			}
			ShowPopup(Get(entry.Node), false);
			if (TooltipShown == entry.Node)
			{
				TooltipShown = {};
			}
			Events.push_back({ UiEventKind::PopupClosed, entry.Node });
			ClearUnavailable(); // Blurs focus inside the popup, cancels presses on it.
			if (focusInside || (!Focused && entry.PriorFocus))
			{
				const auto target = entry.Desc.Anchor && Nodes.contains(entry.Desc.Anchor.Value) && Available(entry.Desc.Anchor) &&
						IsFocusable(Get(entry.Desc.Anchor))
					? entry.Desc.Anchor
					: entry.PriorFocus;
				if (target && Nodes.contains(target.Value) && Available(target) && IsFocusable(Get(target)) && InputAllowed(target))
				{
					Focused = target;
					Events.push_back({ UiEventKind::Focus, target });
					MarkPaintDirty(target);
				}
			}
		}
	}

	void UiDocument::Impl::MoveToTop(UiNodeId id)
	{
		const auto begin = std::find(Order.begin(), Order.end(), id);
		if (begin == Order.end())
		{
			return;
		}
		const auto isInside = [&](UiNodeId other)
		{
			for (auto current = Get(other).Parent; current; current = Get(current).Parent)
			{
				if (current == id)
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
		std::vector<UiNodeId> subtree(begin, end);
		Order.erase(begin, end);
		Order.insert(Order.end(), subtree.begin(), subtree.end());
	}

	void UiDocument::Impl::PlacePopups()
	{
		const UiPoint canvas{ Framebuffer.X / Dpi, Framebuffer.Y / Dpi };
		for (auto& entry : Popups)
		{
			if (!Nodes.contains(entry.Node.Value))
			{
				continue;
			}
			auto& node = Get(entry.Node);
			if (!node.Style.Visible)
			{
				continue;
			}
			const auto& desc = entry.Desc;
			UiRect anchor{ desc.Point.X, desc.Point.Y, 0.0f, 0.0f };
			if (desc.Anchor && Nodes.contains(desc.Anchor.Value) && desc.Side != UiPopupSide::AtPoint)
			{
				anchor = Get(desc.Anchor).Bounds;
			}
			float width = std::min(std::max(node.Desired.X, desc.MatchAnchorWidth ? anchor.Width : 0.0f), canvas.X);
			float height = std::min(node.Desired.Y, canvas.Y);
			float x = anchor.X;
			float y = anchor.Y;
			switch (desc.Side)
			{
			case UiPopupSide::Below:
			case UiPopupSide::Above:
			{
				const float below = anchor.Y + anchor.Height + desc.Offset.Y;
				const float above = anchor.Y - height - desc.Offset.Y;
				const bool fitsBelow = below + height <= canvas.Y;
				const bool fitsAbove = above >= 0.0f;
				const bool preferBelow = desc.Side == UiPopupSide::Below;
				y = preferBelow ? (fitsBelow || !fitsAbove ? below : above) : (fitsAbove || !fitsBelow ? above : below);
				x = anchor.X + desc.Offset.X;
				break;
			}
			case UiPopupSide::Right:
			case UiPopupSide::Left:
			{
				const float right = anchor.X + anchor.Width + desc.Offset.X;
				const float left = anchor.X - width - desc.Offset.X;
				const bool fitsRight = right + width <= canvas.X;
				const bool fitsLeft = left >= 0.0f;
				const bool preferRight = desc.Side == UiPopupSide::Right;
				x = preferRight ? (fitsRight || !fitsLeft ? right : left) : (fitsLeft || !fitsRight ? left : right);
				y = anchor.Y + desc.Offset.Y;
				break;
			}
			case UiPopupSide::AtPoint:
				x = desc.Point.X + desc.Offset.X;
				y = desc.Point.Y + desc.Offset.Y;
				if (x + width > canvas.X)
				{
					x = desc.Point.X - width - desc.Offset.X; // Flip left of the point.
				}
				if (y + height > canvas.Y)
				{
					y = desc.Point.Y - height - desc.Offset.Y; // Flip above the point.
				}
				break;
			case UiPopupSide::Center:
				x = (canvas.X - width) * 0.5f + desc.Offset.X;
				y = (canvas.Y - height) * 0.5f + desc.Offset.Y;
				break;
			}
			x = std::clamp(x, 0.0f, std::max(0.0f, canvas.X - width));
			y = std::clamp(y, 0.0f, std::max(0.0f, canvas.Y - height));
			const UiRect rect{ x, y, width, height };
			if (!Internal::SameRect(rect, node.Bounds))
			{
				ReArrange(node, rect);
			}
			MoveToTop(entry.Node);
		}
	}

	void UiDocument::Impl::LightDismiss(UiPoint point)
	{
		std::optional<std::size_t> closeFrom;
		for (std::size_t i = Popups.size(); i-- > 0;)
		{
			const auto& entry = Popups[i];
			if (!Nodes.contains(entry.Node.Value))
			{
				continue;
			}
			if (Covers(Get(entry.Node).Bounds, point) || !entry.Desc.LightDismiss)
			{
				break;
			}
			if (entry.Desc.Anchor && Nodes.contains(entry.Desc.Anchor.Value) && Covers(Get(entry.Desc.Anchor).Bounds, point))
			{
				break;
			}
			closeFrom = i;
		}
		if (closeFrom)
		{
			ClosePopupsFrom(*closeFrom);
		}
	}

	void UiDocument::Impl::DismissForOpen(UiNodeId anchor, UiNodeId opening)
	{
		HideTooltip(false);
		const auto holder = anchor ? PopupIndexOf(anchor) : std::nullopt;
		std::optional<std::size_t> closeFrom;
		for (std::size_t i = Popups.size(); i-- > 0;)
		{
			if ((holder && i <= *holder) || !Popups[i].Desc.LightDismiss)
			{
				break;
			}
			if (Popups[i].Node != opening)
			{
				closeFrom = i;
			}
		}
		if (closeFrom)
		{
			ClosePopupsFrom(*closeFrom);
		}
	}

	void UiDocument::Impl::CloseOnActivate(UiNodeId activated)
	{
		const auto index = PopupIndexOf(activated);
		// Items (controls, focusable nodes) activate; the popup's background does not.
		if (index && Popups[*index].Desc.CloseOnActivate && activated != Popups[*index].Node && Nodes.contains(activated.Value) &&
			(IsControl(Get(activated)) || IsFocusable(Get(activated))))
		{
			ClosePopupsFrom(*index);
		}
	}

	void UiDocument::Impl::HideTooltip(bool suppress)
	{
		TooltipSuppressed = TooltipSuppressed || suppress;
		TooltipTime = 0.0f;
		if (TooltipShown)
		{
			const auto index = PopupIndexOf(TooltipShown);
			TooltipShown = {};
			if (index)
			{
				ClosePopupsFrom(*index);
			}
		}
	}

	void UiDocument::Impl::UpdateTooltips(float seconds)
	{
		// The deepest laid-out node under the pointer with a tooltip (the node itself need
		// not take input: labels and images can have tooltips).
		UiNodeId target;
		if (HasPointer && !Dirty && !Pressed)
		{
			for (auto it = Order.rbegin(); it != Order.rend(); ++it)
			{
				if (!Nodes.contains(it->Value))
				{
					continue;
				}
				const auto& node = Get(*it);
				if (!node.Active || !node.Bounds.Contains(LastPointer) || !node.Clip.Contains(LastPointer) || !InputAllowed(node.Id))
				{
					continue;
				}
				if (PopupIndexOf(node.Id) && TooltipShown && PopupIndexOf(node.Id) == PopupIndexOf(TooltipShown))
				{
					continue; // The tooltip itself.
				}
				for (auto current = node.Id; current; current = Get(current).Parent)
				{
					if (Get(current).Tooltip && Nodes.contains(Get(current).Tooltip.Value))
					{
						target = current;
						break;
					}
				}
				break;
			}
		}
		if (target != TooltipTarget)
		{
			HideTooltip(false);
			TooltipTarget = target;
			TooltipSuppressed = false;
		}
		if (!target || TooltipSuppressed || TooltipShown)
		{
			return;
		}
		TooltipTime += seconds;
		const auto& owner = Get(target);
		if (TooltipTime < owner.TooltipDelay)
		{
			return;
		}
		UiPopupDesc desc;
		desc.Side = UiPopupSide::AtPoint;
		desc.Point = LastPointer;
		desc.Offset = { 0.0f, 20.0f };
		desc.LightDismiss = false;
		desc.FocusFirst = false;
		const auto tooltip = owner.Tooltip;
		auto& node = Get(tooltip);
		std::erase_if(Popups,
			[&](const PopupEntry& entry)
			{
				return entry.Node == tooltip;
			});
		Popups.push_back({ tooltip, desc, Focused, false });
		ShowPopup(node, true);
		Events.push_back({ UiEventKind::PopupOpened, tooltip });
		TooltipShown = tooltip;
	}

	void UiDocument::OpenPopup(UiNodeId id, const UiPopupDesc& desc)
	{
		auto& node = impl->Get(id);
		if (node.Parent != impl->Root)
		{
			throw std::invalid_argument("A popup must be a child of the UI root");
		}
		if ((desc.Anchor && !impl->Nodes.contains(desc.Anchor.Value)) || !std::isfinite(desc.Point.X) || !std::isfinite(desc.Point.Y) ||
			!std::isfinite(desc.Offset.X) || !std::isfinite(desc.Offset.Y) ||
			static_cast<std::uint8_t>(desc.Side) > static_cast<std::uint8_t>(UiPopupSide::Center))
		{
			throw std::invalid_argument("Invalid UI popup description");
		}
		impl->DismissForOpen(desc.Anchor, id);
		UiNodeId prior = impl->Focused;
		const auto existing = std::find_if(impl->Popups.begin(), impl->Popups.end(),
			[&](const Impl::PopupEntry& entry)
			{
				return entry.Node == id;
			});
		if (existing != impl->Popups.end())
		{
			prior = existing->PriorFocus;
			impl->Popups.erase(existing);
		}
		impl->Popups.push_back({ id, desc, prior, desc.FocusFirst });
		impl->ShowPopup(node, true);
		impl->MarkLayoutDirty(id); // Placed again with the new description.
		impl->Events.push_back({ UiEventKind::PopupOpened, id });
	}

	bool UiDocument::ClosePopup(UiNodeId id)
	{
		for (std::size_t i = 0; i < impl->Popups.size(); ++i)
		{
			if (impl->Popups[i].Node == id)
			{
				impl->ClosePopupsFrom(i);
				return true;
			}
		}
		return false;
	}

	void UiDocument::CloseAllPopups()
	{
		impl->ClosePopupsFrom(0);
	}

	bool UiDocument::IsPopupOpen(UiNodeId id) const
	{
		return std::any_of(impl->Popups.begin(), impl->Popups.end(),
			[&](const Impl::PopupEntry& entry)
			{
				return entry.Node == id;
			});
	}

	UiNodeId UiDocument::GetTopPopup() const
	{
		return impl->Popups.empty() ? UiNodeId{} : impl->Popups.back().Node;
	}

	void UiDocument::SetTooltip(UiNodeId target, UiNodeId tooltip, float delaySeconds)
	{
		auto& node = impl->Get(target);
		if (tooltip && impl->Get(tooltip).Parent != impl->Root)
		{
			throw std::invalid_argument("A tooltip must be a child of the UI root");
		}
		if (!std::isfinite(delaySeconds) || delaySeconds < 0.0f || delaySeconds > 60.0f)
		{
			throw std::invalid_argument("Tooltip delay must be 0 .. 60 seconds");
		}
		node.Tooltip = tooltip;
		node.TooltipDelay = delaySeconds;
		if (tooltip)
		{
			impl->ShowPopup(impl->Get(tooltip), false);
			auto style = impl->Get(tooltip).Style;
			style.HitTest = false;
			style.Focusable = false;
			impl->Get(tooltip).Style = style;
		}
	}

	void UiDocument::SetContextMenu(UiNodeId target, UiNodeId menu)
	{
		auto& node = impl->Get(target);
		if (menu && impl->Get(menu).Parent != impl->Root)
		{
			throw std::invalid_argument("A context menu must be a child of the UI root");
		}
		node.ContextMenu = menu;
		if (menu && !IsPopupOpen(menu))
		{
			impl->ShowPopup(impl->Get(menu), false);
		}
	}

	bool UiDocument::OpenContextMenu(UiPoint framebufferPoint)
	{
		EnsureLayout();
		if (!IsLayoutCurrent() || !std::isfinite(framebufferPoint.X) || !std::isfinite(framebufferPoint.Y))
		{
			return false;
		}
		const UiPoint point{ framebufferPoint.X / impl->Dpi, framebufferPoint.Y / impl->Dpi };
		impl->HideTooltip(true);
		impl->LightDismiss(point);
		EnsureLayout();
		UiNodeId target;
		UiNodeId menu;
		for (auto it = impl->Order.rbegin(); it != impl->Order.rend() && !menu; ++it)
		{
			const auto& node = impl->Get(*it);
			if (!node.Active || !node.Bounds.Contains(point) || !node.Clip.Contains(point) || !impl->InputAllowed(node.Id))
			{
				continue;
			}
			for (auto current = node.Id; current; current = impl->Get(current).Parent)
			{
				if (impl->Get(current).ContextMenu)
				{
					target = current;
					menu = impl->Get(current).ContextMenu;
					break;
				}
			}
			break; // Only the top-most node under the point (and its ancestors).
		}
		if (!menu || !impl->Nodes.contains(menu.Value))
		{
			return false;
		}
		impl->Events.push_back({ UiEventKind::ContextMenu, target });
		UiPopupDesc desc;
		desc.Side = UiPopupSide::AtPoint;
		desc.Point = point;
		desc.CloseOnActivate = true;
		OpenPopup(menu, desc);
		return true;
	}

	void UiDocument::DismissPopups()
	{
		impl->HideTooltip(true);
		impl->LightDismiss({ -Internal::MaxLogical * 4.0f, -Internal::MaxLogical * 4.0f });
	}

	bool UiDocument::OpenContextMenuForFocus()
	{
		for (auto current = impl->Focused; current; current = impl->Get(current).Parent)
		{
			const auto menu = impl->Get(current).ContextMenu;
			if (menu && impl->Nodes.contains(menu.Value))
			{
				impl->Events.push_back({ UiEventKind::ContextMenu, current });
				UiPopupDesc desc;
				desc.Anchor = impl->Focused;
				desc.Side = UiPopupSide::Below;
				desc.CloseOnActivate = true;
				OpenPopup(menu, desc);
				return true;
			}
		}
		return false;
	}

	void UiDocument::ScrollIntoView(UiNodeId id)
	{
		impl->RequireLayout();
		const auto bounds = impl->Get(id).Bounds;
		for (auto current = impl->Get(id).Parent; current; current = impl->Get(current).Parent)
		{
			auto& ancestor = impl->Get(current);
			if (!ancestor.Style.Clip)
			{
				continue;
			}
			const UiRect inner = Internal::ContentBox(ancestor.Bounds, ancestor.Style.Padding);
			UiPoint scroll = ancestor.Scroll;
			// Bounds are as laid out with the current scroll: content = bounds - inner + scroll.
			const float top = bounds.Y - inner.Y + ancestor.ArrangedScroll.Y;
			const float left = bounds.X - inner.X + ancestor.ArrangedScroll.X;
			if (top < scroll.Y)
			{
				scroll.Y = top;
			}
			else if (top + bounds.Height > scroll.Y + inner.Height)
			{
				scroll.Y = top + bounds.Height - inner.Height;
			}
			if (left < scroll.X)
			{
				scroll.X = left;
			}
			else if (left + bounds.Width > scroll.X + inner.Width)
			{
				scroll.X = left + bounds.Width - inner.Width;
			}
			if (scroll.X != ancestor.Scroll.X || scroll.Y != ancestor.Scroll.Y)
			{
				ancestor.Scroll = scroll;
				impl->Dirty = true;
			}
		}
	}
} // namespace Swim::UI
