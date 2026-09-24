#include "Engine/Systems/UI/UiDocument.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace Swim::UI
{
	namespace
	{
		std::atomic<std::uint64_t> nextNodeId{ 1 };
		constexpr std::size_t MaxDepth = 256;

		bool Finite(UiPoint point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y);
		}

		UiRect Intersect(UiRect a, UiRect b)
		{
			const float x = std::max(a.X, b.X);
			const float y = std::max(a.Y, b.Y);
			return { x, y, std::max(0.0f, std::min(a.X + a.Width, b.X + b.Width) - x),
				std::max(0.0f, std::min(a.Y + a.Height, b.Y + b.Height) - y) };
		}

		UiColor Premultiply(UiColor color)
		{
			return { color.R * color.A, color.G * color.A, color.B * color.A, color.A };
		}

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

		void ValidateStyle(const UiStyle& style)
		{
			const auto length = [](UiLength value)
			{
				return (value.Unit == UiUnit::Auto || value.Unit == UiUnit::Logical || value.Unit == UiUnit::Percent) &&
					std::isfinite(value.Value) && value.Value >= 0.0f && value.Value <= (value.Unit == UiUnit::Percent ? 1.0f : 1000000.0f);
			};
			const auto edges = [](UiEdges value)
			{
				const float values[] = { value.Left, value.Top, value.Right, value.Bottom };
				return std::all_of(std::begin(values), std::end(values),
					[](float v)
					{
						return std::isfinite(v) && v >= 0.0f && v <= 1000000.0f;
					});
			};
			const auto color = [](UiColor value)
			{
				return std::isfinite(value.R) && std::isfinite(value.G) && std::isfinite(value.B) && std::isfinite(value.A) &&
					value.R >= 0.0f && value.G >= 0.0f && value.B >= 0.0f && value.A >= 0.0f && value.A <= 1.0f;
			};
			if (!length(style.Width) || !length(style.Height) || !edges(style.Margin) || !edges(style.Padding) || !Finite(style.MinSize) ||
				!Finite(style.MaxSize) || !Finite(style.Offset) || style.MinSize.X < 0.0f || style.MinSize.Y < 0.0f ||
				style.MaxSize.X < style.MinSize.X || style.MaxSize.Y < style.MinSize.Y || style.MaxSize.X > 1000000.0f ||
				style.MaxSize.Y > 1000000.0f || std::abs(style.Offset.X) > 1000000.0f || std::abs(style.Offset.Y) > 1000000.0f ||
				!std::isfinite(style.Gap) || style.Gap < 0.0f || style.Gap > 1000000.0f || !color(style.Background) ||
				!color(style.TextColor) || (style.Flow != UiFlow::Overlay && style.Flow != UiFlow::Row && style.Flow != UiFlow::Column))
			{
				throw std::invalid_argument("Invalid UI style");
			}
		}
	} // namespace

	bool UiRect::Contains(UiPoint point) const
	{
		return point.X >= X && point.Y >= Y && point.X < X + Width && point.Y < Y + Height;
	}

	struct UiDocument::Impl
	{
		struct Node
		{
			UiNodeId Id;
			UiNodeId Parent;
			std::vector<UiNodeId> Children;
			UiStyle Style;
			std::shared_ptr<const Text::FontFace> Face;
			std::vector<Text::ShapedRun> Lines;
			std::string TextContents;
			Text::TextDirection Direction = Text::TextDirection::Auto;
			float FontSize = 16.0f;
			UiPoint TextSize;
			UiPoint Desired;
			UiPoint Scroll;
			UiRect Bounds;
			UiRect Clip;
			bool Active = false;
		};

		std::unordered_map<std::uint64_t, Node> Nodes;
		UiNodeId Root;
		UiNodeId Hover;
		UiNodeId Pressed;
		UiNodeId Focused;
		UiPoint Framebuffer;
		float Dpi = 1.0f;
		bool Dirty = true;
		std::uint64_t Revision = 0;
		std::vector<UiNodeId> Order;
		std::vector<UiPaintQuad> Quads;
		std::vector<UiEvent> Events;

		Node& Get(UiNodeId id) { return Nodes.at(id.Value); }

		const Node& Get(UiNodeId id) const { return Nodes.at(id.Value); }

		void RequireLayout() const
		{
			if (Dirty)
			{
				throw std::logic_error("Call UiDocument::Layout after changing the document");
			}
		}

		std::size_t Depth(UiNodeId id) const
		{
			std::size_t depth = 0;
			for (; id; id = Get(id).Parent)
			{
				++depth;
			}
			return depth;
		}

		std::size_t Height(UiNodeId id) const
		{
			std::size_t height = 1;
			for (const auto child : Get(id).Children)
			{
				height = std::max(height, 1 + Height(child));
			}
			return height;
		}

		bool Available(UiNodeId id) const
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

		void ClearUnavailable()
		{
			if (Focused && (!Available(Focused) || !Get(Focused).Style.Focusable))
			{
				Events.push_back({ UiEventKind::Blur, Focused });
				Focused = {};
			}
			if (Pressed && (!Available(Pressed) || !Get(Pressed).Style.HitTest))
			{
				Events.push_back({ UiEventKind::Cancel, Pressed });
				Pressed = {};
			}
			if (Hover && (!Available(Hover) || !Get(Hover).Style.HitTest))
			{
				Events.push_back({ UiEventKind::Leave, Hover });
				Hover = {};
			}
		}

		void Erase(UiNodeId id)
		{
			for (const auto child : Get(id).Children)
			{
				Erase(child);
			}
			Nodes.erase(id.Value);
		}

		UiPoint Measure(Node& node, UiPoint available)
		{
			if (!node.Style.Visible)
			{
				node.Desired = {};
				return {};
			}
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
			// Root's actual size is the canvas, regardless of its preferred dimensions.
			if (node.Id == Root)
			{
				inner = { std::max(0.0f, available.X - px), std::max(0.0f, available.Y - py) };
			}
			UiPoint content;
			std::size_t count = 0;
			for (const auto childId : node.Children)
			{
				auto& child = Get(childId);
				const auto size = Measure(child, inner);
				if (!child.Style.Visible || child.Style.Absolute)
				{
					continue;
				}
				const float w = size.X + child.Style.Margin.Left + child.Style.Margin.Right;
				const float h = size.Y + child.Style.Margin.Top + child.Style.Margin.Bottom;
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
			content.X = std::max(content.X, node.TextSize.X);
			content.Y = std::max(content.Y, node.TextSize.Y);
			node.Desired = { std::clamp(Resolve(s.Width, available.X, content.X + px), s.MinSize.X, s.MaxSize.X),
				std::clamp(Resolve(s.Height, available.Y, content.Y + py), s.MinSize.Y, s.MaxSize.Y) };
			return node.Desired;
		}

		void Arrange(Node& node, UiRect bounds, UiRect clip, bool enabled)
		{
			const auto& s = node.Style;
			node.Bounds = bounds;
			node.Clip = s.Clip ? Intersect(clip, bounds) : clip;
			node.Active = enabled && s.Enabled;
			Order.push_back(node.Id);
			const UiRect inner{ bounds.X + s.Padding.Left, bounds.Y + s.Padding.Top,
				std::max(0.0f, bounds.Width - s.Padding.Left - s.Padding.Right),
				std::max(0.0f, bounds.Height - s.Padding.Top - s.Padding.Bottom) };
			UiPoint cursor;
			UiPoint extent = node.TextSize;
			std::vector<std::pair<UiNodeId, UiRect>> children;
			for (const auto childId : node.Children)
			{
				auto& child = Get(childId);
				if (!child.Style.Visible)
				{
					continue;
				}
				const auto& cs = child.Style;
				UiRect rect{ (cs.Absolute ? cs.Offset.X : cursor.X) + cs.Margin.Left,
					(cs.Absolute ? cs.Offset.Y : cursor.Y) + cs.Margin.Top,
					std::clamp(Resolve(cs.Width, inner.Width, child.Desired.X), cs.MinSize.X, cs.MaxSize.X),
					std::clamp(Resolve(cs.Height, inner.Height, child.Desired.Y), cs.MinSize.Y, cs.MaxSize.Y) };
				children.emplace_back(childId, rect);
				extent.X = std::max(extent.X, rect.X + rect.Width + cs.Margin.Right);
				extent.Y = std::max(extent.Y, rect.Y + rect.Height + cs.Margin.Bottom);
				if (!cs.Absolute && s.Flow == UiFlow::Row)
				{
					cursor.X += rect.Width + cs.Margin.Left + cs.Margin.Right + s.Gap;
				}
				if (!cs.Absolute && s.Flow == UiFlow::Column)
				{
					cursor.Y += rect.Height + cs.Margin.Top + cs.Margin.Bottom + s.Gap;
				}
			}
			node.Scroll = s.Clip ? UiPoint{ std::clamp(node.Scroll.X, 0.0f, std::max(0.0f, extent.X - inner.Width)),
				std::clamp(node.Scroll.Y, 0.0f, std::max(0.0f, extent.Y - inner.Height)) }
								 : UiPoint{};
			const UiRect childClip = s.Clip ? Intersect(node.Clip, inner) : node.Clip;
			for (auto [id, rect] : children)
			{
				rect.X += inner.X - node.Scroll.X;
				rect.Y += inner.Y - node.Scroll.Y;
				Arrange(Get(id), rect, childClip, node.Active);
			}
		}
	};

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
		if (impl->Nodes.size() >= 65536 || impl->Depth(parent) >= MaxDepth)
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
		impl->Dirty = true;
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
		impl->Dirty = true;
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
		if (impl->Depth(parent) + impl->Height(id) > MaxDepth)
		{
			throw std::length_error("UI hierarchy depth limit reached");
		}
		if (node.Parent == parent)
		{
			return;
		}
		target.Children.push_back(id);
		std::erase(impl->Get(node.Parent).Children, id);
		node.Parent = parent;
		impl->Dirty = true;
		impl->ClearUnavailable();
	}

	void UiDocument::SetStyle(UiNodeId id, const UiStyle& style)
	{
		ValidateStyle(style);
		impl->Get(id).Style = style;
		impl->Dirty = true;
		impl->ClearUnavailable();
	}

	const UiStyle& UiDocument::GetStyle(UiNodeId id) const
	{
		return impl->Get(id).Style;
	}

	void UiDocument::SetText(
		UiNodeId id, std::shared_ptr<const Text::FontFace> face, std::string text, float size, Text::TextDirection direction)
	{
		auto& node = impl->Get(id);
		if (node.Face == face && node.TextContents == text && node.FontSize == size && node.Direction == direction)
		{
			return;
		}
		std::vector<Text::ShapedRun> lines;
		UiPoint measured;
		if (face)
		{
			if (text.size() > 1024u * 1024u)
			{
				throw std::length_error("UI text exceeds 1 MiB");
			}
			const auto metrics = face->GetMetrics(size);
			std::size_t start = 0;
			do
			{
				const auto end = text.find('\n', start);
				const auto count = end == std::string::npos ? text.size() - start : end - start;
				std::string_view line(text.data() + start, count);
				if (!line.empty() && line.back() == '\r')
				{
					line.remove_suffix(1);
				}
				Text::ShapeOptions options;
				options.Direction = direction;
				lines.push_back(face->Shape(line, size, options));
				measured.X = std::max(measured.X, lines.back().AdvanceX);
				measured.Y += metrics.LineHeight;
				if (end == std::string::npos)
				{
					break;
				}
				start = end + 1;
			} while (start <= text.size());
		}
		node.Face = std::move(face);
		node.Lines = std::move(lines);
		node.FontSize = size;
		node.TextContents = std::move(text);
		node.Direction = direction;
		node.TextSize = measured;
		impl->Dirty = true;
	}

	void UiDocument::SetScroll(UiNodeId id, UiPoint offset)
	{
		if (!Finite(offset))
		{
			throw std::invalid_argument("UI scroll offset must be finite");
		}
		impl->Get(id).Scroll = offset;
		impl->Dirty = true;
	}

	UiPoint UiDocument::GetScroll(UiNodeId id) const
	{
		return impl->Get(id).Scroll;
	}

	void UiDocument::Layout(UiPoint framebufferSize, float dpiScale)
	{
		if (!Finite(framebufferSize) || framebufferSize.X < 0.0f || framebufferSize.Y < 0.0f || framebufferSize.X > 1000000.0f ||
			framebufferSize.Y > 1000000.0f || !std::isfinite(dpiScale) || dpiScale < 0.125f || dpiScale > 16.0f)
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
		const UiPoint logical{ framebufferSize.X / dpiScale, framebufferSize.Y / dpiScale };
		auto& root = impl->Get(impl->Root);
		impl->Measure(root, logical);
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

	std::uint64_t UiDocument::GetLayoutRevision() const
	{
		return impl->Revision;
	}

	const std::vector<UiPaintQuad>& UiDocument::Paint(Text::GlyphAtlas& atlas)
	{
		impl->RequireLayout();
		impl->Quads.clear();
		const auto emit = [&](UiPaintQuad quad)
		{
			const auto visible = Intersect(quad.Bounds, quad.Clip);
			if (quad.Color.A > 0.0f && visible.Width > 0.0f && visible.Height > 0.0f)
			{
				impl->Quads.push_back(quad);
			}
		};
		for (const auto id : impl->Order)
		{
			const auto& node = impl->Get(id);
			emit({ id, UiPaintKind::Solid, node.Bounds, node.Clip, Premultiply(node.Style.Background), Text::NoAtlasPage, {}, 0.0f });
			const auto& padding = node.Style.Padding;
			const UiRect content{ node.Bounds.X + padding.Left, node.Bounds.Y + padding.Top,
				std::max(0.0f, node.Bounds.Width - padding.Left - padding.Right),
				std::max(0.0f, node.Bounds.Height - padding.Top - padding.Bottom) };
			const UiRect clip = node.Style.Clip ? Intersect(node.Clip, content) : node.Clip;
			if (clip.Width <= 0.0f || clip.Height <= 0.0f || node.Style.TextColor.A == 0.0f)
			{
				continue;
			}
			float lineTop = content.Y - node.Scroll.Y;
			for (const auto& line : node.Lines)
			{
				float x = content.X - node.Scroll.X;
				const float baseline = lineTop + line.Metrics.Ascender;
				for (const auto& glyph : line.Glyphs)
				{
					const auto entry = atlas.Get(node.Face, glyph.Glyph);
					if (entry.Page != Text::NoAtlasPage)
					{
						const float pageSize = static_cast<float>(atlas.GetDesc().PageSize);
						emit({ id, UiPaintKind::Glyph,
							{ x + glyph.OffsetX + entry.Left * node.FontSize, baseline - glyph.OffsetY - entry.Top * node.FontSize,
								entry.WidthEm * node.FontSize, entry.HeightEm * node.FontSize },
							clip, Premultiply(node.Style.TextColor), entry.Page,
							{ entry.X / pageSize, entry.Y / pageSize, entry.Width / pageSize, entry.Height / pageSize },
							atlas.GetDesc().DistanceRange });
					}
					x += glyph.AdvanceX;
				}
				lineTop += line.Metrics.LineHeight;
			}
		}
		return impl->Quads;
	}

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
			if (node.Active && node.Style.HitTest && node.Bounds.Contains(point) && node.Clip.Contains(point))
			{
				return node.Id;
			}
		}
		return {};
	}

	void UiDocument::PointerMove(UiPoint point)
	{
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
	}

	void UiDocument::PointerDown(UiPoint point)
	{
		PointerMove(point);
		CancelPointer();
		impl->Pressed = impl->Hover;
		Focus(impl->Pressed && impl->Get(impl->Pressed).Style.Focusable ? impl->Pressed : UiNodeId{});
		if (impl->Pressed)
		{
			impl->Events.push_back({ UiEventKind::Press, impl->Pressed });
		}
	}

	void UiDocument::PointerUp(UiPoint point)
	{
		PointerMove(point);
		if (impl->Pressed)
		{
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
		if (impl->Pressed)
		{
			impl->Events.push_back({ UiEventKind::Cancel, impl->Pressed });
			impl->Pressed = {};
		}
	}

	void UiDocument::Focus(UiNodeId id)
	{
		if (id && (!impl->Available(id) || !impl->Get(id).Style.Focusable))
		{
			throw std::invalid_argument("UI focus target is unavailable");
		}
		if (id == impl->Focused)
		{
			return;
		}
		if (impl->Focused)
		{
			impl->Events.push_back({ UiEventKind::Blur, impl->Focused });
		}
		impl->Focused = id;
		if (id)
		{
			impl->Events.push_back({ UiEventKind::Focus, id });
		}
	}

	void UiDocument::FocusNext(bool backwards)
	{
		impl->RequireLayout();
		std::vector<UiNodeId> candidates;
		for (auto id : impl->Order)
		{
			const auto& node = impl->Get(id);
			if (node.Active && node.Style.Focusable)
			{
				candidates.push_back(id);
			}
		}
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

	void UiDocument::ActivateFocused()
	{
		if (impl->Focused)
		{
			impl->Events.push_back({ UiEventKind::Click, impl->Focused });
		}
	}

	UiNodeId UiDocument::GetFocus() const
	{
		return impl->Focused;
	}

	std::vector<UiEvent> UiDocument::DrainEvents()
	{
		std::vector<UiEvent> result;
		result.swap(impl->Events);
		return result;
	}
} // namespace Swim::UI
