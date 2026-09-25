#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"
#include "Engine/Systems/Text/Utf8.h"

#include <atomic>
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

		void ValidateImage(const UiImage& image)
		{
			const bool valid = InRange(image.Size.X, 0.0f, Internal::MaxLogical) && InRange(image.Size.Y, 0.0f, Internal::MaxLogical) &&
				InRange(image.Uv.X, -Internal::MaxLogical, Internal::MaxLogical) &&
				InRange(image.Uv.Y, -Internal::MaxLogical, Internal::MaxLogical) &&
				InRange(image.Uv.Width, -Internal::MaxLogical, Internal::MaxLogical) &&
				InRange(image.Uv.Height, -Internal::MaxLogical, Internal::MaxLogical) && IsValidEdges(image.Slice) &&
				IsValidEdges(image.SliceUv, 1.0f) && IsValidColor(image.Tint) &&
				static_cast<std::uint8_t>(image.Fit) <= static_cast<std::uint8_t>(UiImageFit::Contain);
			if (!valid)
			{
				throw std::invalid_argument("Invalid UI image");
			}
		}
	} // namespace

	namespace Internal
	{
		void ValidateStyle(const UiStyle& s)
		{
			const bool valid = IsValidLength(s.Width) && IsValidLength(s.Height) && IsValidSizeRange(s.MinSize, s.MaxSize) &&
				IsValidEdges(s.Margin) && IsValidEdges(s.Padding) && InRange(s.Offset.X, -MaxLogical, MaxLogical) &&
				InRange(s.Offset.Y, -MaxLogical, MaxLogical) && InRange(s.Gap, 0.0f, MaxLogical) && InRange(s.Grow, 0.0f, MaxLogical) &&
				InRange(s.Shrink, 0.0f, MaxLogical) && InRange(s.AspectRatio, 0.0f, MaxLogical) && IsUnitPoint(s.AnchorMin) &&
				IsUnitPoint(s.AnchorMax) && IsUnitPoint(s.Pivot) && IsValidEnums(s) && IsValidColor(s.Background) &&
				InRange(s.CornerRadius, 0.0f, MaxLogical) && InRange(s.BorderWidth, 0.0f, MaxLogical) && IsValidColor(s.BorderColor) &&
				IsValidColor(s.TextColor) && InRange(s.LineSpacing, 0.25f, 8.0f) && IsValidColor(s.SelectionColor) &&
				IsValidColor(s.CaretColor);
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
			// SelectionColor and CaretColor affects measurement or arrangement.
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
			Events.push_back({ UiEventKind::Blur, Focused });
			if (Nodes.contains(Focused.Value))
			{
				MarkPaintDirty(Focused);
			}
			Focused = {};
			Composition.clear();
		}
		if (Pressed && (!Available(Pressed) || !IsHitTestable(Get(Pressed))))
		{
			Events.push_back({ UiEventKind::Cancel, Pressed });
			Pressed = {};
		}
		if (Hover && (!Available(Hover) || !IsHitTestable(Get(Hover))))
		{
			Events.push_back({ UiEventKind::Leave, Hover });
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
			Get(id).PaintDirty = true;
		}
	}

	UiDocument::UiDocument() : impl(std::make_unique<Impl>())
	{
		impl->Root = { nextNodeId.fetch_add(1, std::memory_order_relaxed) };
		Impl::Node root;
		root.Id = impl->Root;
		impl->Nodes.emplace(root.Id.Value, std::move(root));
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

	void UiDocument::SetStyle(UiNodeId id, const UiStyle& style)
	{
		Internal::ValidateStyle(style);
		auto& node = impl->Get(id);
		const bool paintOnly = Internal::OnlyPaintChanged(node.Style, style);
		node.Style = style;
		if (paintOnly)
		{
			node.PaintDirty = true;
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
			text = Text::SanitizeUtf8(text);
		}
		else
		{
			text.clear();
		}
		if (node.Fonts == fonts && node.TextContents == text && node.FontSize == size && node.TextOptions.Direction == options.Direction &&
			node.TextOptions.Language == options.Language)
		{
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

	void UiDocument::SetImage(UiNodeId id, const UiImage& image)
	{
		ValidateImage(image);
		auto& node = impl->Get(id);
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
		}
		impl->Dirty = false;
		++impl->Revision;
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
} // namespace Swim::UI
