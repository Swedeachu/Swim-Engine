#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <charconv>
#include <cmath>
#include <stdexcept>

// Selection owners (critical-path item 79): radio groups, list views and dropdowns keep
// one selected index (UiControl::Value, -1 for none) among Option nodes registered with
// SetPartRole. Options are hit-testable but never focusable: pressing one focuses its
// owner, and keys go to the owner. A dropdown's options live in a popup (Parts.Popup).
namespace Swim::UI
{
	namespace
	{
		// Rows a page key moves when neither the item extent nor the viewport is known.
		constexpr std::int32_t DefaultPageRows = 10;
		constexpr std::uint32_t MaxItemCount = 1u << 24;

		std::int32_t Index(float value)
		{
			return std::isfinite(value) ? static_cast<std::int32_t>(std::lround(value)) : -1;
		}
	} // namespace

	std::uint32_t UiDocument::Impl::OptionCount(const Node& owner) const
	{
		if (owner.Control.ItemCount > 0)
		{
			return owner.Control.ItemCount;
		}
		std::uint32_t count = 0;
		for (const auto option : owner.Options)
		{
			if (Nodes.contains(option.Value) && Get(option).PartOf == owner.Id && Get(option).Role == UiPartRole::Option)
			{
				++count;
			}
		}
		return count;
	}

	UiNodeId UiDocument::Impl::OptionFor(const Node& owner, std::int32_t index) const
	{
		if (index < 0)
		{
			return {};
		}
		for (const auto option : owner.Options)
		{
			if (Nodes.contains(option.Value))
			{
				const auto& node = Get(option);
				if (node.PartOf == owner.Id && node.Role == UiPartRole::Option && Index(node.PartValue) == index)
				{
					return option;
				}
			}
		}
		return {};
	}

	bool UiDocument::Impl::IsDropdownOpen(const Node& owner) const
	{
		const auto popup = owner.Control.Parts.Popup;
		return owner.Control.Kind == UiControlKind::Dropdown && popup &&
			std::any_of(Popups.begin(), Popups.end(),
				[&](const PopupEntry& entry)
				{
					return entry.Node == popup;
				});
	}

	void UiDocument::Impl::SyncOwner(Node& owner)
	{
		auto& c = owner.Control;
		if (c.Kind != UiControlKind::Dropdown || !c.Parts.Label || !Nodes.contains(c.Parts.Label.Value))
		{
			return;
		}
		auto& label = Get(c.Parts.Label);
		const auto option = OptionFor(owner, Index(c.Value));
		if (!label.Fonts || !option)
		{
			return; // No choice (or an unbound virtual row): the label keeps its text (a placeholder).
		}
		// The option's own text, else its first descendant with text.
		const std::string* text = nullptr;
		std::vector<UiNodeId> pending{ option };
		while (!pending.empty() && !text)
		{
			const auto id = pending.front();
			pending.erase(pending.begin());
			const auto& node = Get(id);
			if (!node.TextContents.empty())
			{
				text = &node.TextContents;
				break;
			}
			pending.insert(pending.end(), node.Children.begin(), node.Children.end());
		}
		if (text && label.TextContents != *text)
		{
			label.TextContents = *text;
			label.TextLayout.reset();
			label.MeasureLayout.reset();
			label.Selection = ClampSelection(label, label.Selection);
			MarkLayoutDirty(label.Id);
		}
	}

	bool UiDocument::Impl::SelectOption(Node& owner, std::int32_t index, bool commit)
	{
		auto& c = owner.Control;
		if (c.ReadOnly)
		{
			return false;
		}
		const auto count = static_cast<std::int32_t>(OptionCount(owner));
		index = count == 0 ? -1 : std::clamp(index, -1, count - 1);
		const bool changed = index != Index(c.Value);
		if (changed)
		{
			c.Value = static_cast<float>(index);
			MarkSubtreeVisualDirty(owner.Id);
			SyncOwner(owner);
			Events.push_back({ UiEventKind::ValueChanged, owner.Id, c.Value });
			if (commit)
			{
				Events.push_back({ UiEventKind::ValueCommitted, owner.Id, c.Value });
			}
		}
		if (index >= 0)
		{
			RevealOption(owner, index);
		}
		return changed;
	}

	void UiDocument::Impl::RevealOption(Node& owner, std::int32_t index)
	{
		const auto& c = owner.Control;
		if (index < 0 || !c.ScrollTarget || !Nodes.contains(c.ScrollTarget.Value))
		{
			return;
		}
		auto& target = Get(c.ScrollTarget);
		const bool horizontal = c.Kind == UiControlKind::ListView && c.Orientation == UiOrientation::Horizontal;
		const UiRect inner = Internal::ContentBox(target.Bounds, target.Style.Padding);
		const float viewport = horizontal ? inner.Width : inner.Height;
		float start = 0.0f;
		float length = 0.0f;
		if (c.ItemExtent > 0.0f)
		{
			start = static_cast<float>(index) * c.ItemExtent;
			length = c.ItemExtent;
		}
		else
		{
			const auto option = OptionFor(owner, index);
			if (!option || !Get(option).Active)
			{
				return;
			}
			const auto& bounds = Get(option).Bounds;
			// Bounds are laid out with the current scroll: content = bounds - inner + scroll.
			start = horizontal ? bounds.X - inner.X + target.ArrangedScroll.X : bounds.Y - inner.Y + target.ArrangedScroll.Y;
			length = horizontal ? bounds.Width : bounds.Height;
		}
		float& scroll = horizontal ? target.Scroll.X : target.Scroll.Y;
		float wanted = scroll;
		if (start < wanted)
		{
			wanted = start;
		}
		else if (start + length > wanted + viewport)
		{
			wanted = start + length - viewport;
		}
		if (wanted != scroll)
		{
			scroll = std::max(0.0f, wanted); // Clamped to the content by the next Layout.
			Dirty = true;
		}
	}

	void UiDocument::Impl::ToggleDropdown(Node& owner)
	{
		auto& c = owner.Control;
		if (c.Kind != UiControlKind::Dropdown || !c.Parts.Popup || !Nodes.contains(c.Parts.Popup.Value))
		{
			return;
		}
		if (IsDropdownOpen(owner))
		{
			for (std::size_t i = 0; i < Popups.size(); ++i)
			{
				if (Popups[i].Node == c.Parts.Popup)
				{
					ClosePopupsFrom(i);
					break;
				}
			}
			return;
		}
		if (c.ReadOnly)
		{
			return;
		}
		DismissForOpen(owner.Id, c.Parts.Popup);
		UiPopupDesc desc;
		desc.Anchor = owner.Id;
		desc.Side = UiPopupSide::Below;
		desc.MatchAnchorWidth = true;
		desc.FocusFirst = false; // Focus stays on the dropdown; keys move the highlight.
		std::erase_if(Popups,
			[&](const PopupEntry& entry)
			{
				return entry.Node == c.Parts.Popup;
			});
		Popups.push_back({ c.Parts.Popup, desc, Focused, false, true });
		ShowPopup(Get(c.Parts.Popup), true);
		Events.push_back({ UiEventKind::PopupOpened, c.Parts.Popup });
		owner.Highlight = std::max(0, Index(c.Value));
		MarkSubtreeVisualDirty(c.Parts.Popup);
		RevealOption(owner, owner.Highlight);
	}

	void UiDocument::Impl::OptionPressed(Node& option, bool inside)
	{
		if (!inside || !option.PartOf || !Nodes.contains(option.PartOf.Value))
		{
			return;
		}
		auto& owner = Get(option.PartOf);
		if (!IsSelectionOwner(owner) || !Available(owner.Id))
		{
			return;
		}
		SelectOption(owner, Index(option.PartValue), true);
		if (owner.Control.Kind == UiControlKind::Dropdown && IsDropdownOpen(owner))
		{
			ToggleDropdown(owner); // Closes.
		}
	}

	bool UiDocument::Impl::OwnerKey(Node& owner, UiKey key)
	{
		auto& c = owner.Control;
		const auto count = static_cast<std::int32_t>(OptionCount(owner));
		const auto value = Index(c.Value);
		if (c.Kind == UiControlKind::Dropdown)
		{
			const bool open = IsDropdownOpen(owner);
			if (key == UiKey::Enter || key == UiKey::Space)
			{
				if (open && owner.Highlight >= 0 && owner.Highlight < count)
				{
					SelectOption(owner, owner.Highlight, true);
				}
				ToggleDropdown(owner);
				return true;
			}
			std::int32_t next = open ? owner.Highlight : value;
			switch (key)
			{
			case UiKey::Up:
				next = next < 0 ? 0 : next - 1;
				break;
			case UiKey::Down:
				next = next < 0 ? 0 : next + 1;
				break;
			case UiKey::Home:
				next = 0;
				break;
			case UiKey::End:
				next = count - 1;
				break;
			default:
				return false;
			}
			if (count == 0)
			{
				return true;
			}
			next = std::clamp(next, 0, count - 1);
			if (open)
			{
				if (next != owner.Highlight)
				{
					owner.Highlight = next;
					if (c.Parts.Popup && Nodes.contains(c.Parts.Popup.Value))
					{
						MarkSubtreeVisualDirty(c.Parts.Popup);
					}
				}
				RevealOption(owner, next);
				return true;
			}
			SelectOption(owner, next, true);
			return true;
		}
		const bool horizontal = c.Orientation == UiOrientation::Horizontal;
		if (c.Kind == UiControlKind::RadioGroup)
		{
			std::int32_t delta = 0;
			switch (key)
			{
			case UiKey::Left:
			case UiKey::Up:
				delta = -1;
				break;
			case UiKey::Right:
			case UiKey::Down:
				delta = 1;
				break;
			case UiKey::Home:
				SelectOption(owner, 0, true);
				return true;
			case UiKey::End:
				SelectOption(owner, count - 1, true);
				return true;
			case UiKey::Space:
			case UiKey::Enter:
				if (value < 0 && count > 0)
				{
					SelectOption(owner, 0, true);
					return true;
				}
				return false; // Activation (a Click).
			default:
				return false;
			}
			if (count > 0)
			{
				// Wraps around, like platform radio groups.
				const auto next = value < 0 ? (delta > 0 ? 0 : count - 1) : (value + delta + count) % count;
				SelectOption(owner, next, true);
			}
			return true;
		}
		// List views.
		std::int32_t page = DefaultPageRows;
		if (c.ScrollTarget && Nodes.contains(c.ScrollTarget.Value))
		{
			const auto& target = Get(c.ScrollTarget);
			const UiRect inner = Internal::ContentBox(target.Bounds, target.Style.Padding);
			float extent = c.ItemExtent;
			if (extent <= 0.0f)
			{
				if (const auto option = OptionFor(owner, std::max(0, value)); option && Get(option).Active)
				{
					extent = horizontal ? Get(option).Bounds.Width : Get(option).Bounds.Height;
				}
			}
			if (extent > 0.0f)
			{
				page = std::max(1, static_cast<std::int32_t>((horizontal ? inner.Width : inner.Height) / extent));
			}
		}
		std::int32_t next = value;
		const auto previousKey = horizontal ? UiKey::Left : UiKey::Up;
		const auto nextKey = horizontal ? UiKey::Right : UiKey::Down;
		if (key == previousKey)
		{
			next = value < 0 ? 0 : value - 1;
		}
		else if (key == nextKey)
		{
			next = value < 0 ? 0 : value + 1;
		}
		else if (key == UiKey::PageUp)
		{
			next = value < 0 ? 0 : value - page;
		}
		else if (key == UiKey::PageDown)
		{
			next = value < 0 ? 0 : value + page;
		}
		else if (key == UiKey::Home)
		{
			next = 0;
		}
		else if (key == UiKey::End)
		{
			next = count - 1;
		}
		else if (key == UiKey::Enter)
		{
			if (value >= 0)
			{
				Events.push_back({ UiEventKind::Submit, owner.Id, c.Value });
			}
			return true;
		}
		else
		{
			return false; // The cross axis navigates.
		}
		if (count > 0)
		{
			SelectOption(owner, std::clamp(next, 0, count - 1), true);
		}
		return true;
	}

	void UiDocument::Impl::CommitValueLabel(Node& label)
	{
		if (label.Role != UiPartRole::Label || !label.PartOf || !Nodes.contains(label.PartOf.Value))
		{
			return;
		}
		auto& slider = Get(label.PartOf);
		if (slider.Control.Kind != UiControlKind::Slider || slider.Control.Parts.Label != label.Id)
		{
			return;
		}
		// Plain decimal numbers, surrounding spaces and a leading '+' allowed; anything else
		// restores the displayed value.
		std::string_view text = label.TextContents;
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
		if (!text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(parsed) &&
			!slider.Control.ReadOnly && Available(slider.Id))
		{
			ChangeValue(slider, parsed, true);
		}
		// Reformat (also when the value did not change, or the text was rejected).
		const auto keep = label.TextContents;
		label.TextContents.clear();
		SyncValueLabel(slider, true);
		if (label.TextContents.empty())
		{
			label.TextContents = keep; // Not a formatted label (no decimals or fonts).
		}
	}

	void UiDocument::SetPartRole(UiNodeId partId, UiNodeId controlId, UiPartRole role, float value)
	{
		auto& part = impl->Get(partId);
		const auto release = [&]
		{
			if (part.Role == UiPartRole::Option)
			{
				if (part.PartOf && impl->Nodes.contains(part.PartOf.Value))
				{
					std::erase(impl->Get(part.PartOf).Options, partId);
				}
				part.Control.Kind = UiControlKind::None;
				impl->ClearUnavailable();
			}
			part.PartOf = {};
			part.Role = UiPartRole::None;
			impl->MarkLayoutDirty(partId);
			impl->MarkSubtreeVisualDirty(partId);
		};
		if (role == UiPartRole::None)
		{
			release();
			return;
		}
		auto& control = impl->Get(controlId);
		if (role == UiPartRole::Option)
		{
			const auto isDescendant = [&]
			{
				for (auto current = part.Parent; current; current = impl->Get(current).Parent)
				{
					if (current == controlId)
					{
						return true;
					}
				}
				return false;
			}();
			const auto kind = control.Control.Kind;
			const bool owner = impl->IsSelectionOwner(control);
			// Radio and list options live inside their owner; a dropdown's in its popup.
			if (!owner || !std::isfinite(value) || value < 0.0f || value >= float(MaxItemCount) || value != std::floor(value) ||
				partId == controlId || (kind != UiControlKind::Dropdown && !isDescendant) ||
				(part.Control.Kind != UiControlKind::None && part.Control.Kind != UiControlKind::Option))
			{
				throw std::invalid_argument("Options are nodes without another control, inside a radio group or list view (or anywhere "
											"for a dropdown), at integral indices");
			}
			if (part.Role == UiPartRole::Option && part.PartOf == controlId && part.PartValue == value)
			{
				return;
			}
			if (part.Role != UiPartRole::None)
			{
				release();
			}
			part.PartOf = controlId;
			part.Role = role;
			part.PartValue = value;
			part.Control = {};
			part.Control.Kind = UiControlKind::Option;
			control.Options.push_back(partId);
			impl->MarkLayoutDirty(partId);
			impl->MarkSubtreeVisualDirty(partId);
			if (Index(control.Control.Value) == Index(value))
			{
				impl->SyncOwner(control);
			}
			return;
		}
		if (role != UiPartRole::Tick || control.Control.Kind != UiControlKind::Slider || part.Parent != controlId || !std::isfinite(value))
		{
			throw std::invalid_argument("SetPartRole registers slider tick marks (direct children) at finite values, and options");
		}
		if (part.Role == UiPartRole::Option)
		{
			release();
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

	UiNodeId UiDocument::FindOption(UiNodeId owner, std::uint32_t index) const
	{
		const auto& node = impl->Get(owner);
		return index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
			? UiNodeId{}
			: impl->OptionFor(node, static_cast<std::int32_t>(index));
	}

	std::uint32_t UiDocument::GetOptionCount(UiNodeId owner) const
	{
		return impl->OptionCount(impl->Get(owner));
	}
} // namespace Swim::UI
