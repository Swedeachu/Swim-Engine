#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"
#include "Engine/Systems/Text/Utf8.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace Swim::UI
{

	namespace
	{

		std::atomic<std::uint64_t> nextNodeId{ 1 };

		bool Finite(UiPoint point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y);
		}

		bool InRange(float value, float low, float high)
		{
			return std::isfinite(value) && value >= low && value <= high;
		}

		bool IsValidLength(UiLength length)
		{
			switch (length.Unit)
			{
			case UiUnit::Auto:
			case UiUnit::Logical:
				return InRange(length.Value, 0.0f, Internal::MaxLogical);
			case UiUnit::Percent:
				return InRange(length.Value, 0.0f, 1.0f);
			}

			return false;
		}

		bool IsValidEdges(const UiEdges& edges, float high = Internal::MaxLogical)
		{
			return InRange(edges.Left, 0.0f, high) && InRange(edges.Top, 0.0f, high) && InRange(edges.Right, 0.0f, high) &&
				InRange(edges.Bottom, 0.0f, high);
		}

		bool IsValidColor(const UiColor& color)
		{
			return InRange(color.R, 0.0f, Internal::MaxLogical) && InRange(color.G, 0.0f, Internal::MaxLogical) &&
				InRange(color.B, 0.0f, Internal::MaxLogical) && InRange(color.A, 0.0f, 1.0f);
		}

		bool IsValidSizeRange(UiPoint minimum, UiPoint maximum)
		{
			return InRange(minimum.X, 0.0f, Internal::MaxLogical) && InRange(minimum.Y, 0.0f, Internal::MaxLogical) &&
				InRange(maximum.X, minimum.X, Internal::MaxLogical) && InRange(maximum.Y, minimum.Y, Internal::MaxLogical);
		}

		bool IsUnitPoint(UiPoint point)
		{
			return InRange(point.X, 0.0f, 1.0f) && InRange(point.Y, 0.0f, 1.0f);
		}

		bool IsValidEnums(const UiStyle& s)
		{
			return static_cast<std::uint8_t>(s.Flow) <= static_cast<std::uint8_t>(UiFlow::Column) &&
				static_cast<std::uint8_t>(s.Justify) <= static_cast<std::uint8_t>(UiJustify::SpaceEvenly) &&
				static_cast<std::uint8_t>(s.AlignItems) <= static_cast<std::uint8_t>(UiAlign::Stretch) &&
				static_cast<std::uint8_t>(s.AlignSelf) <= static_cast<std::uint8_t>(UiAlign::Stretch) &&
				static_cast<std::uint8_t>(s.TextAlign) <= static_cast<std::uint8_t>(Text::TextAlign::Center) &&
				static_cast<std::uint8_t>(s.TextWrap) <= static_cast<std::uint8_t>(Text::TextWrap::Word);
		}

		// Fix(value, low, high, fallback): clamp, or the fallback when not finite.
		bool Fix(float& value, float low, float high, float fallback)
		{
			const float fixed = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
			const bool changed = !(fixed == value);
			value = fixed;
			return changed;
		}

		bool FixPoint(UiPoint& point, float low, float high, UiPoint fallback)
		{
			const bool x = Fix(point.X, low, high, fallback.X);
			const bool y = Fix(point.Y, low, high, fallback.Y);
			return x || y;
		}

		bool FixEdges(UiEdges& edges, float high = Internal::MaxLogical)
		{
			bool changed = false;

			for (float* e : { &edges.Left, &edges.Top, &edges.Right, &edges.Bottom })
			{
				changed |= Fix(*e, 0.0f, high, 0.0f);
			}

			return changed;
		}

		bool FixColor(UiColor& color, const UiColor& fallback)
		{
			bool changed = Fix(color.R, 0.0f, Internal::MaxLogical, fallback.R);
			changed |= Fix(color.G, 0.0f, Internal::MaxLogical, fallback.G);
			changed |= Fix(color.B, 0.0f, Internal::MaxLogical, fallback.B);
			changed |= Fix(color.A, 0.0f, 1.0f, fallback.A);
			return changed;
		}

		bool FixLength(UiLength& length)
		{
			if (static_cast<std::uint8_t>(length.Unit) > static_cast<std::uint8_t>(UiUnit::Percent))
			{
				length = {};
				return true;
			}

			return Fix(length.Value, 0.0f, length.Unit == UiUnit::Percent ? 1.0f : Internal::MaxLogical, 0.0f);
		}

		template <typename E> bool FixEnum(E& value, E last, E fallback)
		{
			if (static_cast<std::uint8_t>(value) > static_cast<std::uint8_t>(last))
			{
				value = fallback;
				return true;
			}

			return false;
		}

	} // namespace

	bool SanitizeUiStyle(UiStyle& s)
	{
		static const UiStyle d;
		const float max = Internal::MaxLogical;
		bool c = FixLength(s.Width);
		c |= FixLength(s.Height);
		c |= FixPoint(s.MinSize, 0.0f, max, d.MinSize);
		c |= Fix(s.MaxSize.X, s.MinSize.X, max, d.MaxSize.X);
		c |= Fix(s.MaxSize.Y, s.MinSize.Y, max, d.MaxSize.Y);
		c |= FixEdges(s.Margin);
		c |= FixEdges(s.Padding);
		c |= FixEnum(s.Flow, UiFlow::Column, d.Flow);
		c |= Fix(s.Gap, 0.0f, max, d.Gap);
		c |= Fix(s.Grow, 0.0f, max, d.Grow);
		c |= Fix(s.Shrink, 0.0f, max, d.Shrink);
		c |= FixEnum(s.Justify, UiJustify::SpaceEvenly, d.Justify);
		c |= FixEnum(s.AlignItems, UiAlign::Stretch, d.AlignItems);
		c |= FixEnum(s.AlignSelf, UiAlign::Stretch, d.AlignSelf);
		c |= Fix(s.AspectRatio, 0.0f, max, d.AspectRatio);
		c |= FixPoint(s.Offset, -max, max, d.Offset);
		c |= FixPoint(s.AnchorMin, 0.0f, 1.0f, d.AnchorMin);
		c |= FixPoint(s.AnchorMax, 0.0f, 1.0f, d.AnchorMax);
		c |= FixPoint(s.Pivot, 0.0f, 1.0f, d.Pivot);
		c |= FixColor(s.Background, d.Background);
		c |= FixEnum(s.Gradient, UiGradient::Vertical, d.Gradient);
		c |= FixColor(s.BackgroundEnd, d.BackgroundEnd);
		c |= Fix(s.CornerRadius, 0.0f, max, d.CornerRadius);
		c |= Fix(s.BorderWidth, 0.0f, max, d.BorderWidth);
		c |= FixColor(s.BorderColor, d.BorderColor);
		c |= FixColor(s.TextColor, d.TextColor);
		c |= FixEnum(s.TextAlign, Text::TextAlign::Center, d.TextAlign);
		c |= FixEnum(s.TextWrap, Text::TextWrap::Word, d.TextWrap);
		c |= Fix(s.LineSpacing, 0.25f, 8.0f, d.LineSpacing);
		c |= FixColor(s.SelectionColor, d.SelectionColor);
		c |= FixColor(s.CaretColor, d.CaretColor);
		c |= Fix(s.Opacity, 0.0f, 1.0f, d.Opacity);
		c |= Fix(s.TransitionSeconds, 0.0f, 60.0f, d.TransitionSeconds);
		return c;
	}

	bool SanitizeUiImage(UiImage& image)
	{
		static const UiImage d;
		const float max = Internal::MaxLogical;
		bool c = FixPoint(image.Size, 0.0f, max, d.Size);
		c |= Fix(image.Uv.X, -max, max, d.Uv.X);
		c |= Fix(image.Uv.Y, -max, max, d.Uv.Y);
		c |= Fix(image.Uv.Width, -max, max, d.Uv.Width);
		c |= Fix(image.Uv.Height, -max, max, d.Uv.Height);
		c |= FixEdges(image.Slice);
		c |= FixEdges(image.SliceUv, 1.0f);
		c |= FixColor(image.Tint, d.Tint);
		c |= FixEnum(image.Fit, UiImageFit::Contain, d.Fit);
		c |= FixEnum(image.Blend, UiBlend::Invert, d.Blend);
		c |= Fix(image.Glint, 0.0f, 4.0f, d.Glint);
		return c;
	}

	namespace Internal
	{

		void ValidateImage(const UiImage& image)
		{
			const bool valid = InRange(image.Size.X, 0.0f, Internal::MaxLogical) && InRange(image.Size.Y, 0.0f, Internal::MaxLogical) &&
				InRange(image.Uv.X, -Internal::MaxLogical, Internal::MaxLogical) &&
				InRange(image.Uv.Y, -Internal::MaxLogical, Internal::MaxLogical) &&
				InRange(image.Uv.Width, -Internal::MaxLogical, Internal::MaxLogical) &&
				InRange(image.Uv.Height, -Internal::MaxLogical, Internal::MaxLogical) && IsValidEdges(image.Slice) &&
				IsValidEdges(image.SliceUv, 1.0f) && IsValidColor(image.Tint) &&
				static_cast<std::uint8_t>(image.Fit) <= static_cast<std::uint8_t>(UiImageFit::Contain) &&
				static_cast<std::uint8_t>(image.Blend) <= static_cast<std::uint8_t>(UiBlend::Invert);

			if (!valid)
			{
				throw std::invalid_argument("Invalid UI image");
			}
		}

		void ValidateVisual(const UiVisual& v)
		{
			for (const auto* color : { &v.Background, &v.BorderColor, &v.TextColor, &v.ImageTint })
			{
				if (*color && !IsValidColor(**color))
				{
					throw std::invalid_argument("Invalid UI state rule color");
				}
			}

			if ((v.BorderWidth && !InRange(*v.BorderWidth, 0.0f, MaxLogical)) ||
				(v.CornerRadius && !InRange(*v.CornerRadius, 0.0f, MaxLogical)) || (v.Opacity && !InRange(*v.Opacity, 0.0f, 1.0f)))
			{
				throw std::invalid_argument("Invalid UI state rule value");
			}

			if (v.Image)
			{
				ValidateImage(*v.Image);
			}
		}

		void ValidateStyle(const UiStyle& s)
		{
			const bool valid = IsValidLength(s.Width) && IsValidLength(s.Height) && IsValidSizeRange(s.MinSize, s.MaxSize) &&
				IsValidEdges(s.Margin) && IsValidEdges(s.Padding) && InRange(s.Offset.X, -MaxLogical, MaxLogical) &&
				InRange(s.Offset.Y, -MaxLogical, MaxLogical) && InRange(s.Gap, 0.0f, MaxLogical) && InRange(s.Grow, 0.0f, MaxLogical) &&
				InRange(s.Shrink, 0.0f, MaxLogical) && InRange(s.AspectRatio, 0.0f, MaxLogical) && IsUnitPoint(s.AnchorMin) &&
				IsUnitPoint(s.AnchorMax) && IsUnitPoint(s.Pivot) && IsValidEnums(s) && IsValidColor(s.Background) &&
				InRange(s.CornerRadius, 0.0f, MaxLogical) && InRange(s.BorderWidth, 0.0f, MaxLogical) && IsValidColor(s.BorderColor) &&
				IsValidColor(s.TextColor) && InRange(s.LineSpacing, 0.25f, 8.0f) && IsValidColor(s.SelectionColor) &&
				IsValidColor(s.CaretColor) && InRange(s.Opacity, 0.0f, 1.0f) && InRange(s.TransitionSeconds, 0.0f, 60.0f);

			if (!valid)
			{
				throw std::invalid_argument("Invalid UI style");
			}
		}

		bool OnlyPaintChanged(const UiStyle& a, const UiStyle& b)
		{
			const auto point = [](UiPoint x, UiPoint y)
			{
				return x.X == y.X && x.Y == y.Y;
			};
			const auto edges = [](const UiEdges& x, const UiEdges& y)
			{
				return x.Left == y.Left && x.Top == y.Top && x.Right == y.Right && x.Bottom == y.Bottom;
			};
			const auto length = [](UiLength x, UiLength y)
			{
				return x.Unit == y.Unit && x.Value == y.Value;
			};
			// Everything except Background, CornerRadius, BorderWidth/Color, TextColor,
			// SelectionColor, CaretColor, Opacity, TransitionSeconds and TabIndex affects
			// measurement or arrangement.
			return length(a.Width, b.Width) && length(a.Height, b.Height) && point(a.MinSize, b.MinSize) && point(a.MaxSize, b.MaxSize) &&
				edges(a.Margin, b.Margin) && edges(a.Padding, b.Padding) && a.Flow == b.Flow && a.Gap == b.Gap && a.Grow == b.Grow &&
				a.Shrink == b.Shrink && a.Justify == b.Justify && a.AlignItems == b.AlignItems && a.AlignSelf == b.AlignSelf &&
				a.AspectRatio == b.AspectRatio && a.Absolute == b.Absolute && point(a.Offset, b.Offset) &&
				point(a.AnchorMin, b.AnchorMin) && point(a.AnchorMax, b.AnchorMax) && point(a.Pivot, b.Pivot) && a.Clip == b.Clip &&
				a.Visible == b.Visible && a.Enabled == b.Enabled && a.HitTest == b.HitTest && a.Focusable == b.Focusable &&
				a.TextAlign == b.TextAlign && a.TextWrap == b.TextWrap && a.LineSpacing == b.LineSpacing;
		}

	} // namespace Internal

	bool UiRect::Contains(UiPoint point) const
	{
		return point.X >= X && point.Y >= Y && point.X < X + Width && point.Y < Y + Height;
	}

	void UiDocument::Impl::RequireLayout() const
	{
		if (Dirty)
		{
			throw std::logic_error("Call UiDocument::Layout after changing the document");
		}
	}

	std::size_t UiDocument::Impl::Depth(UiNodeId id) const
	{
		std::size_t depth = 0;

		for (; id; id = Get(id).Parent)
		{
			++depth;
		}

		return depth;
	}

	std::size_t UiDocument::Impl::Height(UiNodeId id) const
	{
		std::size_t height = 1;

		for (const auto child : Get(id).Children)
		{
			height = std::max(height, 1 + Height(child));
		}

		return height;
	}

	bool UiDocument::Impl::Available(UiNodeId id) const
	{
		if (!id || !Nodes.contains(id.Value))
		{
			return false;
		}

		for (; id; id = Get(id).Parent)
		{
			const auto& style = Get(id).Style;

			if (!style.Visible || !style.Enabled)
			{
				return false;
			}
		}

		return true;
	}

	void UiDocument::Impl::ClearUnavailable()
	{
		if (Focused && (!Available(Focused) || !IsFocusable(Get(Focused))))
		{
			QueueEvent({ UiEventKind::Blur, Focused });

			if (Nodes.contains(Focused.Value))
			{
				MarkPaintDirty(Focused);
			}

			Focused = {};
			Composition.clear();
		}

		if (Pressed && (!Available(Pressed) || !IsHitTestable(Get(Pressed))))
		{
			if (Dragging == Pressed && Nodes.contains(Dragging.Value))
			{
				CancelCapture();
			}

			QueueEvent({ UiEventKind::Cancel, Pressed });
			Pressed = {};
		}

		if (Dragging && (!Nodes.contains(Dragging.Value) || Dragging != Pressed))
		{
			Dragging = {};
		}

		if (Hover && (!Available(Hover) || !IsHitTestable(Get(Hover))))
		{
			QueueEvent({ UiEventKind::Leave, Hover });
			Hover = {};
		}

		if (Selecting && (!Nodes.contains(Selecting.Value) || Selecting != Pressed))
		{
			Selecting = {};
		}
	}

	void UiDocument::Impl::Erase(UiNodeId id)
	{
		for (const auto child : Get(id).Children)
		{
			Erase(child);
		}

		Nodes.erase(id.Value);
	}

	void UiDocument::Impl::MarkLayoutDirty(UiNodeId id)
	{
		Dirty = true;

		if (!id || !Nodes.contains(id.Value))
		{
			return;
		}

		auto& node = Get(id);
		node.MeasureDirty = true;
		node.PaintDirty = true;
		MarkVisualDirty(node);

		for (auto parent = node.Parent; parent; parent = Get(parent).Parent)
		{
			auto& ancestor = Get(parent);

			if (ancestor.SubtreeDirty)
			{
				break; // Already propagated above.
			}

			ancestor.SubtreeDirty = true;
		}
	}

	void UiDocument::Impl::MarkPaintDirty(UiNodeId id)
	{
		if (id && Nodes.contains(id.Value))
		{
			auto& node = Get(id);
			node.PaintDirty = true;
			MarkVisualDirty(node);
		}
	}

	UiDocument::UiDocument() : impl(std::make_unique<Impl>())
	{
		impl->Owner = this;
		impl->Root = { nextNodeId.fetch_add(1, std::memory_order_relaxed) };
		Impl::Node root;
		root.Id = impl->Root;
		impl->Nodes.emplace(root.Id.Value, std::move(root));
		impl->Theme = std::make_shared<const UiTheme>();
		impl->Classes = impl->Theme->Build();
	}

	UiDocument::~UiDocument() = default;

	UiNodeId UiDocument::GetRoot() const
	{
		return impl->Root;
	}

	UiNodeId UiDocument::Create(UiNodeId parent)
	{
		auto& parentNode = impl->Get(parent);

		if (impl->Nodes.size() >= Internal::MaxNodes || impl->Depth(parent) >= Internal::MaxDepth)
		{
			throw std::length_error("UI document node or hierarchy depth limit reached");
		}

		Impl::Node node;
		node.Id = { nextNodeId.fetch_add(1, std::memory_order_relaxed) };
		node.Parent = parent;
		const auto id = node.Id;
		parentNode.Children.push_back(id);

		try
		{
			impl->Nodes.emplace(id.Value, std::move(node));
		}
		catch (...)
		{
			parentNode.Children.pop_back();
			throw;
		}

		impl->MarkLayoutDirty(parent);
		return id;
	}

	bool UiDocument::Contains(UiNodeId node) const
	{
		return impl->Nodes.contains(node.Value);
	}

	bool UiDocument::Remove(UiNodeId id)
	{
		if (!Contains(id) || id == impl->Root)
		{
			return false;
		}

		const auto parent = impl->Get(id).Parent;
		std::erase(impl->Get(parent).Children, id);
		impl->Erase(id);
		std::erase_if(impl->Popups,
			[&](const Impl::PopupEntry& entry)
			{
				return !impl->Nodes.contains(entry.Node.Value);
			});

		if (impl->TooltipShown && !impl->Nodes.contains(impl->TooltipShown.Value))
		{
			impl->TooltipShown = {};
		}

		impl->ClearUnavailable();
		impl->MarkLayoutDirty(parent);
		return true;
	}

	void UiDocument::Reparent(UiNodeId id, UiNodeId parent)
	{
		auto& node = impl->Get(id);
		auto& target = impl->Get(parent);

		if (id == impl->Root)
		{
			throw std::invalid_argument("Cannot reparent the UI root");
		}

		for (auto ancestor = parent; ancestor; ancestor = impl->Get(ancestor).Parent)
		{
			if (ancestor == id)
			{
				throw std::invalid_argument("UI reparent would create a cycle");
			}
		}

		if (impl->Depth(parent) + impl->Height(id) > Internal::MaxDepth)
		{
			throw std::length_error("UI hierarchy depth limit reached");
		}

		if (node.Parent == parent)
		{
			return;
		}

		const auto previous = node.Parent;
		target.Children.push_back(id);
		std::erase(impl->Get(previous).Children, id);
		node.Parent = parent;
		impl->MarkLayoutDirty(previous);
		impl->MarkLayoutDirty(id);
		impl->ClearUnavailable();
	}

	void UiDocument::SetStyle(UiNodeId id, const UiStyle& requested)
	{
		auto& node = impl->Get(id);
		UiStyle style = requested;

		if (SanitizeUiStyle(style) && !node.SanitizeWarned)
		{
			node.SanitizeWarned = true;
			std::cerr << "[UI] Out-of-range style values clamped on node " << id.Value << '\n';
		}

		const bool paintOnly = Internal::OnlyPaintChanged(node.Style, style);
		node.Style = style;

		if (paintOnly)
		{
			node.PaintDirty = true;
			impl->MarkVisualDirty(node);
			return;
		}

		impl->MarkLayoutDirty(id);
		impl->ClearUnavailable();
	}

	const UiStyle& UiDocument::GetStyle(UiNodeId id) const
	{
		return impl->Get(id).Style;
	}

	void UiDocument::SetText(
		UiNodeId id, std::shared_ptr<const Text::FontCollection> fonts, std::string text, float size, const UiTextOptions& options)
	{
		auto& node = impl->Get(id);

		if (fonts)
		{
			if (text.size() > 1024u * 1024u)
			{
				throw std::length_error("UI text exceeds 1 MiB");
			}

			if (!std::isfinite(size) || size <= 0.0f || size > 16384.0f || options.Language.size() > 128 ||
				(options.Direction != Text::TextDirection::Auto && options.Direction != Text::TextDirection::LeftToRight &&
					options.Direction != Text::TextDirection::RightToLeft))
			{
				throw std::invalid_argument("Invalid UI text size or options");
			}

			const auto byBegin = [](const auto& a, const auto& b)
			{
				return a.Begin < b.Begin;
			};

			if (!std::is_sorted(options.Styles.begin(), options.Styles.end(), byBegin) ||
				!std::is_sorted(options.Colors.begin(), options.Colors.end(), byBegin))
			{
				throw std::invalid_argument("UI text spans must be sorted by Begin");
			}

			text = Text::SanitizeUtf8(text);
		}
		else
		{
			text.clear();
		}

		node.Obfuscated = std::any_of(options.Styles.begin(), options.Styles.end(),
			[](const UiTextStyleSpan& span)
			{
				return HasFormat(span.Format, UiTextFormat::Obfuscated);
			});

		if (node.Fonts == fonts && node.TextContents == text && node.FontSize == size && node.TextOptions.Direction == options.Direction &&
			node.TextOptions.Language == options.Language && Internal::BoldRanges(node.TextOptions) == Internal::BoldRanges(options))
		{
			if (node.TextOptions.Colors != options.Colors || node.TextOptions.Styles != options.Styles ||
				node.TextOptions.Shadow != options.Shadow || node.TextOptions.PixelSnap != options.PixelSnap)
			{
				node.TextOptions.Colors = options.Colors;
				node.TextOptions.Styles = options.Styles;
				node.TextOptions.Shadow = options.Shadow;
				node.TextOptions.PixelSnap = options.PixelSnap;
				impl->MarkPaintDirty(id);
			}

			return;
		}

		node.Fonts = std::move(fonts);
		node.TextContents = std::move(text);
		node.FontSize = size;
		node.TextOptions = options;
		node.TextLayout.reset();
		node.MeasureLayout.reset();
		node.Selection = impl->ClampSelection(node, node.Selection);

		if (node.Id == impl->Focused)
		{
			impl->Composition.clear();
		}

		impl->MarkLayoutDirty(id);
	}

	void UiDocument::SetText(
		UiNodeId id, std::shared_ptr<const Text::FontFace> face, std::string text, float size, Text::TextDirection direction)
	{
		UiTextOptions options;
		options.Direction = direction;
		SetText(id, face ? Text::FontCollection::Single(std::move(face)) : nullptr, std::move(text), size, options);
	}

	const std::string& UiDocument::GetText(UiNodeId id) const
	{
		return impl->Get(id).TextContents;
	}

	const Text::TextLayout* UiDocument::GetTextLayout(UiNodeId id) const
	{
		return impl->Get(id).TextLayout.get();
	}

	void UiDocument::SetImage(UiNodeId id, const UiImage& requested)
	{
		auto& node = impl->Get(id);
		UiImage image = requested;

		if (SanitizeUiImage(image) && !node.SanitizeWarned)
		{
			node.SanitizeWarned = true;
			std::cerr << "[UI] Out-of-range image values clamped on node " << id.Value << '\n';
		}

		node.HasImage = true;
		node.Image = image;
		impl->MarkLayoutDirty(id);
	}

	void UiDocument::ClearImage(UiNodeId id)
	{
		auto& node = impl->Get(id);

		if (node.HasImage)
		{
			node.HasImage = false;
			impl->MarkLayoutDirty(id);
		}
	}

	void UiDocument::SetScroll(UiNodeId id, UiPoint offset)
	{
		if (!Finite(offset))
		{
			throw std::invalid_argument("UI scroll offset must be finite");
		}

		impl->Get(id).Scroll = offset;
		impl->Dirty = true; // Arrangement only; measurement is unaffected.
	}

	UiPoint UiDocument::GetScroll(UiNodeId id) const
	{
		return impl->Get(id).Scroll;
	}

	float UiDocument::GetDpiScale() const
	{
		return impl->Dpi;
	}

	void UiDocument::Layout(UiPoint framebufferSize, float dpiScale)
	{
		if (!Finite(framebufferSize) || framebufferSize.X < 0.0f || framebufferSize.Y < 0.0f || framebufferSize.X > Internal::MaxLogical ||
			framebufferSize.Y > Internal::MaxLogical || !std::isfinite(dpiScale) || dpiScale < 0.125f || dpiScale > 16.0f)
		{
			throw std::invalid_argument("Invalid UI canvas or DPI scale");
		}

		if (!impl->Dirty && framebufferSize.X == impl->Framebuffer.X && framebufferSize.Y == impl->Framebuffer.Y && dpiScale == impl->Dpi)
		{
			return;
		}

		impl->Dirty = true;
		impl->Framebuffer = framebufferSize;
		impl->Dpi = dpiScale;
		impl->MeasuredNodes = 0;
		const UiPoint logical{ framebufferSize.X / dpiScale, framebufferSize.Y / dpiScale };
		auto& root = impl->Get(impl->Root);
		impl->Measure(root, logical, logical.X);
		impl->Order.clear();
		// Hidden nodes are omitted from traversal; clear stale query bounds too.
		for (auto& [id, node] : impl->Nodes)
		{
			node.Bounds = {};
			node.Clip = {};
			node.Active = false;
		}

		if (root.Style.Visible)
		{
			impl->Arrange(root, { 0, 0, logical.X, logical.Y }, { 0, 0, logical.X, logical.Y }, true);
			impl->NotifyArranged(); // Controls that follow other nodes (scroll bars and their targets).
			impl->PlacePopups();
		}

		impl->Dirty = false;
		++impl->Revision;
		// Popups opened with FocusFirst take focus once they are laid out (focus changes may
		// close popups, so the stack is re-read each step).
		bool focused = false;

		for (std::size_t i = 0; i < impl->Popups.size(); ++i)
		{
			auto& entry = impl->Popups[i];
			const auto popup = entry.Node;

			if (entry.PendingNotify)
			{
				entry.PendingNotify = false;
				const auto anchor = entry.Desc.Anchor;

				if (auto* behavior = anchor && impl->Nodes.contains(anchor.Value) ? impl->Get(anchor).Behavior.get() : nullptr)
				{
					auto context = impl->Context(anchor);
					behavior->OnPopupLaidOut(context, popup); // A dropdown reveals its highlight.
				}
			}

			if (!impl->Popups[i].PendingFocus)
			{
				continue;
			}

			impl->Popups[i].PendingFocus = false;

			for (const auto id : impl->TabOrder())
			{
				const auto index = impl->PopupIndexOf(id);

				if (index && impl->Popups[*index].Node == popup)
				{
					Focus(id);
					focused = true;
					break;
				}
			}
		}

		if (!focused && impl->Focused && !impl->InputAllowed(impl->Focused))
		{
			Focus({}); // A modal opened without anything to focus: nothing below it keeps focus.
		}

		if (impl->Dirty)
		{
			Layout(framebufferSize, dpiScale); // Focus changes that relayout (editable text).
		}
	}

	UiRect UiDocument::GetBounds(UiNodeId id) const
	{
		impl->RequireLayout();
		return impl->Get(id).Bounds;
	}

	bool UiDocument::IsLayoutCurrent() const
	{
		return !impl->Dirty && impl->Revision > 0;
	}

	std::uint64_t UiDocument::GetLayoutRevision() const
	{
		return impl->Revision;
	}

	std::uint32_t UiDocument::GetMeasuredNodeCount() const
	{
		return impl->MeasuredNodes;
	}

	std::uint32_t UiDocument::GetRepaintedNodeCount() const
	{
		return impl->RepaintedNodes;
	}

	std::vector<UiEvent> UiDocument::DrainEvents()
	{
		std::vector<UiEvent> result;
		result.swap(impl->Events);
		return result;
	}

	void UiDocument::DiscardEvents()
	{
		impl->Events.clear();
	}

} // namespace Swim::UI
