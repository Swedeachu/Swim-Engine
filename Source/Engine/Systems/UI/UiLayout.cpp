#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <algorithm>

namespace Swim::UI
{
	namespace
	{
		using Internal::Unbounded;

		float Resolve(UiLength length, float available, float measured)
		{
			if (length.Unit == UiUnit::Logical)
			{
				return length.Value;
			}
			if (length.Unit == UiUnit::Percent)
			{
				return length.Value * available;
			}
			return measured;
		}

		// Width / height follow each other when one axis is Auto (width wins when both are).
		void ApplyAspect(const UiStyle& s, float& width, float& height)
		{
			if (s.AspectRatio <= 0.0f)
			{
				return;
			}
			if (s.Height.Unit == UiUnit::Auto)
			{
				height = std::clamp(width / s.AspectRatio, s.MinSize.Y, s.MaxSize.Y);
			}
			else if (s.Width.Unit == UiUnit::Auto)
			{
				width = std::clamp(height * s.AspectRatio, s.MinSize.X, s.MaxSize.X);
			}
		}

		UiAlign ResolveAlign(const UiStyle& parent, const UiStyle& child)
		{
			if (child.AlignSelf != UiAlign::Auto)
			{
				return child.AlignSelf;
			}
			return parent.AlignItems == UiAlign::Auto ? UiAlign::Start : parent.AlignItems;
		}

		// Offset of a child of size `size` (+ margins) inside `room` on one axis.
		float AlignOffset(UiAlign align, float room, float size, float marginStart, float marginEnd)
		{
			switch (align)
			{
			case UiAlign::Center:
				return (room - size - marginStart - marginEnd) * 0.5f + marginStart;
			case UiAlign::End:
				return room - size - marginEnd;
			default:
				return marginStart;
			}
		}

		bool SameDesc(const Text::TextLayoutDesc& a, const Text::TextLayoutDesc& b)
		{
			return a.Size == b.Size && a.Direction == b.Direction && a.Align == b.Align && a.Wrap == b.Wrap && a.MaxWidth == b.MaxWidth &&
				a.LineSpacing == b.LineSpacing && a.Language == b.Language && a.Kerning == b.Kerning && a.Ligatures == b.Ligatures;
		}

		// Glyph positions of an unwrapped layout whose every line is left-aligned do not
		// depend on the alignment box, so the measured layout can be displayed as is.
		bool IsBoxIndependent(const Text::TextLayout& layout)
		{
			const auto& desc = layout.GetDesc();
			if (desc.Wrap != Text::TextWrap::None)
			{
				return false;
			}
			for (const auto& line : layout.GetLines())
			{
				const bool rtl = (line.BaseLevel & 1) != 0;
				const bool left = desc.Align == Text::TextAlign::Left || (desc.Align == Text::TextAlign::Start && !rtl) ||
					(desc.Align == Text::TextAlign::End && rtl);
				if (!left)
				{
					return false;
				}
			}
			return true;
		}
	} // namespace

	std::string UiDocument::Impl::DisplayText(const Node& node) const
	{
		if (!IsComposing(node))
		{
			return node.TextContents;
		}
		const auto caret = std::min<std::size_t>(node.Selection.Caret, node.TextContents.size());
		std::string text = node.TextContents.substr(0, caret);
		text += Composition;
		text += node.TextContents.substr(caret);
		return text;
	}

	std::shared_ptr<const Text::TextLayout> UiDocument::Impl::BuildTextLayout(const Node& node, float box) const
	{
		Text::TextLayoutDesc desc;
		desc.Size = node.FontSize;
		desc.Direction = node.TextOptions.Direction;
		desc.Language = node.TextOptions.Language;
		desc.Align = node.Style.TextAlign;
		desc.Wrap = node.Style.TextWrap;
		desc.LineSpacing = node.Style.LineSpacing;
		desc.MaxWidth = box;
		for (const auto* existing : { node.TextLayout.get(), node.MeasureLayout.get() })
		{
			if (existing && SameDesc(existing->GetDesc(), desc) && existing->GetText() == DisplayText(node))
			{
				return existing == node.TextLayout.get() ? node.TextLayout : node.MeasureLayout;
			}
		}
		return std::make_shared<const Text::TextLayout>(DisplayText(node), node.Fonts, desc);
	}

	const Text::TextLayout& UiDocument::Impl::EditLayout(Node& node)
	{
		const float box = node.TextLayout ? node.TextLayout->GetDesc().MaxWidth : Unbounded;
		node.TextLayout = BuildTextLayout(node, box);
		return *node.TextLayout;
	}

	UiPoint UiDocument::Impl::Measure(Node& node, UiPoint available, float wrap)
	{
		if (!node.Style.Visible)
		{
			node.Desired = {};
			return {};
		}
		if (!node.MeasureDirty && !node.SubtreeDirty && node.CachedAvailable.X == available.X && node.CachedAvailable.Y == available.Y &&
			node.CachedWrap == wrap)
		{
			return node.Desired;
		}
		++MeasuredNodes;
		const auto& s = node.Style;
		const float px = s.Padding.Left + s.Padding.Right;
		const float py = s.Padding.Top + s.Padding.Bottom;
		UiPoint inner{ std::max(0.0f, std::clamp(Resolve(s.Width, available.X, available.X), s.MinSize.X, s.MaxSize.X) - px),
			std::max(0.0f, std::clamp(Resolve(s.Height, available.Y, available.Y), s.MinSize.Y, s.MaxSize.Y) - py) };
		// Percent children of an intrinsic (Auto) axis contribute zero to
		// measurement, then resolve against the final content box in Arrange.
		if (s.Width.Unit == UiUnit::Auto)
		{
			inner.X = 0.0f;
		}
		if (s.Height.Unit == UiUnit::Auto)
		{
			inner.Y = 0.0f;
		}
		// Content width known before measuring children: the wrap width of this node's
		// text and the room its children may wrap into.
		float definite = Unbounded;
		if (node.Id == Root)
		{
			// Root's actual size is the canvas, regardless of its preferred dimensions.
			inner = { std::max(0.0f, available.X - px), std::max(0.0f, available.Y - py) };
			definite = inner.X;
		}
		else if (s.Width.Unit == UiUnit::Logical || (s.Width.Unit == UiUnit::Percent && available.X > 0.0f))
		{
			definite = inner.X;
		}
		const float room = std::max(0.0f, std::min(std::isfinite(definite) ? definite : wrap - px, s.MaxSize.X - px));

		node.TextSize = {};
		if (node.Fonts)
		{
			const float box = s.TextWrap == Text::TextWrap::Word && std::isfinite(room) ? std::max(room, 1e-3f) : Unbounded;
			node.MeasureLayout = BuildTextLayout(node, box);
			node.TextSize = { node.MeasureLayout->GetWidth(), node.MeasureLayout->GetHeight() };
		}
		else
		{
			node.MeasureLayout.reset();
		}
		const UiPoint image = node.HasImage ? node.Image.Size : UiPoint{};

		UiPoint content;
		std::size_t count = 0;
		const float childRoom = std::isfinite(definite) ? definite : std::max(0.0f, wrap - px);
		for (const auto childId : node.Children)
		{
			auto& child = Get(childId);
			const auto& cs = child.Style;
			const auto size = Measure(child, inner, std::max(0.0f, childRoom - cs.Margin.Left - cs.Margin.Right));
			if (!cs.Visible || cs.Absolute)
			{
				continue;
			}
			const float w = size.X + cs.Margin.Left + cs.Margin.Right;
			const float h = size.Y + cs.Margin.Top + cs.Margin.Bottom;
			if (s.Flow == UiFlow::Row)
			{
				content.X += w + (count ? s.Gap : 0.0f);
				content.Y = std::max(content.Y, h);
			}
			else if (s.Flow == UiFlow::Column)
			{
				content.X = std::max(content.X, w);
				content.Y += h + (count ? s.Gap : 0.0f);
			}
			else
			{
				content.X = std::max(content.X, w);
				content.Y = std::max(content.Y, h);
			}
			++count;
		}
		content.X = std::max({ content.X, node.TextSize.X, image.X });
		content.Y = std::max({ content.Y, node.TextSize.Y, image.Y });
		float width = std::clamp(Resolve(s.Width, available.X, content.X + px), s.MinSize.X, s.MaxSize.X);
		float height = std::clamp(Resolve(s.Height, available.Y, content.Y + py), s.MinSize.Y, s.MaxSize.Y);
		ApplyAspect(s, width, height);
		node.Desired = { width, height };
		node.CachedAvailable = available;
		node.CachedWrap = wrap;
		node.MeasureDirty = false;
		node.SubtreeDirty = false;
		return node.Desired;
	}

	void UiDocument::Impl::ArrangeChildren(
		Node& node, const UiRect& inner, UiPoint& extent, std::vector<std::pair<UiNodeId, UiRect>>& placed)
	{
		const auto& s = node.Style;

		struct Item
		{
			Node* Child = nullptr;
			UiRect Rect;
			std::size_t Slot = 0; // Index in `placed`, which keeps paint/tab order equal to child order.
		};

		std::vector<Item> flow;
		for (const auto childId : node.Children)
		{
			auto& child = Get(childId);
			const auto& cs = child.Style;
			if (!cs.Visible)
			{
				continue;
			}
			float width = std::clamp(Resolve(cs.Width, inner.Width, child.Desired.X), cs.MinSize.X, cs.MaxSize.X);
			float height = std::clamp(Resolve(cs.Height, inner.Height, child.Desired.Y), cs.MinSize.Y, cs.MaxSize.Y);
			ApplyAspect(cs, width, height);
			if (!cs.Absolute)
			{
				flow.push_back({ &child, { 0, 0, width, height }, placed.size() });
				placed.emplace_back(child.Id, UiRect{});
				continue;
			}
			// Anchored placement relative to the content box.
			UiRect rect{ 0, 0, width, height };
			if (cs.AnchorMax.X > cs.AnchorMin.X)
			{
				rect.Width = std::clamp(
					(cs.AnchorMax.X - cs.AnchorMin.X) * inner.Width - cs.Margin.Left - cs.Margin.Right, cs.MinSize.X, cs.MaxSize.X);
				rect.X = cs.AnchorMin.X * inner.Width + cs.Margin.Left + cs.Offset.X;
			}
			else
			{
				rect.X = cs.AnchorMin.X * inner.Width + cs.Offset.X + cs.Margin.Left - cs.Pivot.X * rect.Width;
			}
			if (cs.AnchorMax.Y > cs.AnchorMin.Y)
			{
				rect.Height = std::clamp(
					(cs.AnchorMax.Y - cs.AnchorMin.Y) * inner.Height - cs.Margin.Top - cs.Margin.Bottom, cs.MinSize.Y, cs.MaxSize.Y);
				rect.Y = cs.AnchorMin.Y * inner.Height + cs.Margin.Top + cs.Offset.Y;
			}
			else
			{
				rect.Y = cs.AnchorMin.Y * inner.Height + cs.Offset.Y + cs.Margin.Top - cs.Pivot.Y * rect.Height;
			}
			placed.emplace_back(child.Id, rect);
		}

		if (s.Flow == UiFlow::Row || s.Flow == UiFlow::Column)
		{
			const bool row = s.Flow == UiFlow::Row;
			const auto mainOf = [&](UiRect& r) -> float&
			{
				return row ? r.Width : r.Height;
			};
			const auto crossOf = [&](UiRect& r) -> float&
			{
				return row ? r.Height : r.Width;
			};
			const auto mainMargins = [&](const UiStyle& cs)
			{
				return row ? cs.Margin.Left + cs.Margin.Right : cs.Margin.Top + cs.Margin.Bottom;
			};
			const auto mainLimits = [&](const UiStyle& cs)
			{
				return row ? std::pair{ cs.MinSize.X, cs.MaxSize.X } : std::pair{ cs.MinSize.Y, cs.MaxSize.Y };
			};
			const float innerMain = row ? inner.Width : inner.Height;
			const float innerCross = row ? inner.Height : inner.Width;
			const auto freeSpace = [&]
			{
				float used = s.Gap * float(flow.empty() ? 0 : flow.size() - 1);
				for (auto& item : flow)
				{
					used += mainOf(item.Rect) + mainMargins(item.Child->Style);
				}
				return innerMain - used;
			};
			float free = freeSpace();
			float grow = 0.0f;
			float shrink = 0.0f;
			for (auto& item : flow)
			{
				grow += item.Child->Style.Grow;
				shrink += item.Child->Style.Shrink * mainOf(item.Rect);
			}
			if (free > 0.0f && grow > 0.0f)
			{
				for (auto& item : flow)
				{
					const auto [low, high] = mainLimits(item.Child->Style);
					mainOf(item.Rect) = std::clamp(mainOf(item.Rect) + free * item.Child->Style.Grow / grow, low, high);
				}
				free = freeSpace();
			}
			else if (free < 0.0f && shrink > 0.0f)
			{
				const float overflow = -free;
				for (auto& item : flow)
				{
					const auto [low, high] = mainLimits(item.Child->Style);
					const float share = overflow * item.Child->Style.Shrink * mainOf(item.Rect) / shrink;
					mainOf(item.Rect) = std::clamp(mainOf(item.Rect) - share, low, high);
				}
				free = freeSpace();
			}
			const float spare = std::max(0.0f, free);
			const float n = float(flow.size());
			float leading = 0.0f;
			float between = 0.0f;
			switch (s.Justify)
			{
			case UiJustify::Center:
				leading = spare * 0.5f;
				break;
			case UiJustify::End:
				leading = spare;
				break;
			case UiJustify::SpaceBetween:
				between = flow.size() > 1 ? spare / (n - 1.0f) : 0.0f;
				break;
			case UiJustify::SpaceAround:
				between = n > 0.0f ? spare / n : 0.0f;
				leading = between * 0.5f;
				break;
			case UiJustify::SpaceEvenly:
				between = spare / (n + 1.0f);
				leading = between;
				break;
			default:
				break;
			}
			float cursor = leading;
			for (auto& item : flow)
			{
				const auto& cs = item.Child->Style;
				const UiAlign align = ResolveAlign(s, cs);
				const float crossStart = row ? cs.Margin.Top : cs.Margin.Left;
				const float crossEnd = row ? cs.Margin.Bottom : cs.Margin.Right;
				const bool crossAuto = row ? cs.Height.Unit == UiUnit::Auto : cs.Width.Unit == UiUnit::Auto;
				if (align == UiAlign::Stretch && crossAuto)
				{
					crossOf(item.Rect) = std::max(0.0f,
						std::clamp(
							innerCross - crossStart - crossEnd, row ? cs.MinSize.Y : cs.MinSize.X, row ? cs.MaxSize.Y : cs.MaxSize.X));
				}
				const float cross = AlignOffset(align, innerCross, crossOf(item.Rect), crossStart, crossEnd);
				if (row)
				{
					item.Rect.X = cursor + cs.Margin.Left;
					item.Rect.Y = cross;
					cursor += item.Rect.Width + cs.Margin.Left + cs.Margin.Right + s.Gap + between;
				}
				else
				{
					item.Rect.Y = cursor + cs.Margin.Top;
					item.Rect.X = cross;
					cursor += item.Rect.Height + cs.Margin.Top + cs.Margin.Bottom + s.Gap + between;
				}
			}
		}
		else
		{
			// Overlay: every child at the content origin, aligned on both axes.
			for (auto& item : flow)
			{
				const auto& cs = item.Child->Style;
				const UiAlign align = ResolveAlign(s, cs);
				if (align == UiAlign::Stretch)
				{
					if (cs.Width.Unit == UiUnit::Auto)
					{
						item.Rect.Width =
							std::max(0.0f, std::clamp(inner.Width - cs.Margin.Left - cs.Margin.Right, cs.MinSize.X, cs.MaxSize.X));
					}
					if (cs.Height.Unit == UiUnit::Auto)
					{
						item.Rect.Height =
							std::max(0.0f, std::clamp(inner.Height - cs.Margin.Top - cs.Margin.Bottom, cs.MinSize.Y, cs.MaxSize.Y));
					}
				}
				item.Rect.X = AlignOffset(align, inner.Width, item.Rect.Width, cs.Margin.Left, cs.Margin.Right);
				item.Rect.Y = AlignOffset(align, inner.Height, item.Rect.Height, cs.Margin.Top, cs.Margin.Bottom);
			}
		}
		for (auto& item : flow)
		{
			placed[item.Slot].second = item.Rect;
		}
		// Control parts with geometry roles (slider track/fill/thumb, scroll bar thumb,
		// toggle knob) are placed by their control instead of the flow.
		for (auto& [id, rect] : placed)
		{
			const auto& child = Get(id);
			if (child.PartOf)
			{
				if (const auto geometry = PartGeometry(child, inner))
				{
					rect = *geometry;
				}
			}
		}
		for (const auto& [id, rect] : placed)
		{
			const auto& cs = Get(id).Style;
			extent.X = std::max(extent.X, rect.X + rect.Width + cs.Margin.Right);
			extent.Y = std::max(extent.Y, rect.Y + rect.Height + cs.Margin.Bottom);
		}
	}

	void UiDocument::Impl::RevealCaret(Node& node, const UiRect& inner)
	{
		node.RevealCaret = false;
		if (!node.Style.Clip || !node.TextLayout)
		{
			return;
		}
		const std::uint32_t caretOffset = IsComposing(node)
			? node.Selection.Caret + std::min<std::uint32_t>(CompositionCursor, static_cast<std::uint32_t>(Composition.size()))
			: node.Selection.Caret;
		const auto caret = node.TextLayout->GetCaret(caretOffset);
		const float right = caret.X + 1.0f;
		if (caret.X < node.Scroll.X)
		{
			node.Scroll.X = caret.X;
		}
		else if (right > node.Scroll.X + inner.Width)
		{
			node.Scroll.X = right - inner.Width;
		}
		if (caret.Top < node.Scroll.Y)
		{
			node.Scroll.Y = caret.Top;
		}
		else if (caret.Top + caret.Height > node.Scroll.Y + inner.Height)
		{
			node.Scroll.Y = caret.Top + caret.Height - inner.Height;
		}
	}

	void UiDocument::Impl::Arrange(Node& node, UiRect bounds, UiRect clip, bool enabled)
	{
		const auto& s = node.Style;
		node.Bounds = bounds;
		node.Clip = s.Clip ? Internal::Intersect(clip, bounds) : clip;
		node.Active = enabled && s.Enabled;
		Order.push_back(node.Id);
		const UiRect inner = Internal::ContentBox(bounds, s.Padding);
		UiPoint extent;
		if (node.Fonts)
		{
			// The displayed layout uses the final content width (wrapping and alignment box).
			if (node.MeasureLayout && IsBoxIndependent(*node.MeasureLayout))
			{
				node.TextLayout = node.MeasureLayout;
			}
			else
			{
				node.TextLayout = BuildTextLayout(node, std::max(inner.Width, 1e-3f));
			}
			extent = { node.TextLayout->GetWidth(), node.TextLayout->GetHeight() };
		}
		else
		{
			node.TextLayout.reset();
		}
		if (node.HasImage)
		{
			extent.X = std::max(extent.X, std::min(node.Image.Size.X, inner.Width));
			extent.Y = std::max(extent.Y, std::min(node.Image.Size.Y, inner.Height));
		}
		std::vector<std::pair<UiNodeId, UiRect>> placed;
		ArrangeChildren(node, inner, extent, placed);
		node.MaxScroll = { std::max(0.0f, extent.X - inner.Width), std::max(0.0f, extent.Y - inner.Height) };
		if (node.RevealCaret)
		{
			RevealCaret(node, inner);
		}
		node.Scroll = s.Clip
			? UiPoint{ std::clamp(node.Scroll.X, 0.0f, node.MaxScroll.X), std::clamp(node.Scroll.Y, 0.0f, node.MaxScroll.Y) }
			: UiPoint{};
		const UiRect childClip = s.Clip ? Internal::Intersect(node.Clip, inner) : node.Clip;
		for (auto [id, rect] : placed)
		{
			rect.X += inner.X - node.Scroll.X;
			rect.Y += inner.Y - node.Scroll.Y;
			Arrange(Get(id), rect, childClip, node.Active);
		}
	}
} // namespace Swim::UI
