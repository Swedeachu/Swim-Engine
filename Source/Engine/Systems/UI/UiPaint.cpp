#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Swim::UI
{

	namespace
	{

		using Internal::Intersect;
		using Internal::Premultiply;

		// A glyph's padded atlas quad at a pen position on the baseline (screen
		// coordinates, positive down); atlas entries are em units, positive up.
		UiPaintQuad GlyphQuad(UiNodeId node, const Text::AtlasGlyph& entry, const Text::GlyphAtlasDesc& atlas, UiPoint origin,
			float fontSize, const UiRect& clip, const UiColor& color)
		{
			const float page = static_cast<float>(atlas.PageSize);
			UiPaintQuad quad;
			quad.Node = node;
			quad.Kind = entry.Color ? UiPaintKind::ColorGlyph : UiPaintKind::Glyph;
			quad.Bounds = { origin.X + entry.Left * fontSize, origin.Y - entry.Top * fontSize, entry.WidthEm * fontSize,
				entry.HeightEm * fontSize };
			quad.Clip = clip;
			quad.Color = color;
			quad.AtlasPage = entry.Page;
			quad.Uv = { entry.X / page, entry.Y / page, entry.Width / page, entry.Height / page };
			quad.DistanceRange = atlas.DistanceRange;
			return quad;
		}

		UiColor Fade(const UiColor& premultiplied, float opacity)
		{
			return { premultiplied.R * opacity, premultiplied.G * opacity, premultiplied.B * opacity, premultiplied.A * opacity };
		}

		UiPaintQuad SolidQuad(UiNodeId node, const UiRect& bounds, const UiRect& clip, const UiColor& premultiplied)
		{
			UiPaintQuad quad;
			quad.Node = node;
			quad.Bounds = bounds;
			quad.Clip = clip;
			quad.Color = premultiplied;
			return quad;
		}

		bool Visible(const UiPaintQuad& quad)
		{
			const auto visible = Intersect(quad.Bounds, quad.Clip);
			const bool opaque = quad.Color.A > 0.0f ||
				(quad.Kind == UiPaintKind::Solid &&
					((quad.BorderWidth > 0.0f && quad.BorderColor.A > 0.0f) ||
						(quad.Gradient != UiGradient::None && quad.ColorEnd.A > 0.0f)));
			return opaque && visible.Width > 0.0f && visible.Height > 0.0f;
		}

		// The image's destination inside the content box.
		UiRect FitImage(const UiImage& image, const UiRect& box)
		{
			if (image.Fit != UiImageFit::Contain || image.Size.X <= 0.0f || image.Size.Y <= 0.0f || box.Width <= 0.0f || box.Height <= 0.0f)
			{
				return box;
			}

			const float scale = std::min(box.Width / image.Size.X, box.Height / image.Size.Y);
			const float width = image.Size.X * scale;
			const float height = image.Size.Y * scale;
			return { box.X + (box.Width - width) * 0.5f, box.Y + (box.Height - height) * 0.5f, width, height };
		}

		// One image quad, or nine when slice borders are set. Borders keep their size;
		// edges stretch along one axis and the centre along both. Borders larger than the
		// destination shrink proportionally.
		template <typename Emit>
		void AddImage(UiNodeId node, const UiImage& image, const UiColor& tint, const UiRect& dest, const UiRect& clip, Emit emit)
		{
			UiPaintQuad base;
			base.Node = node;
			base.Kind = UiPaintKind::Image;
			base.Clip = clip;
			base.Color = tint;
			base.Texture = image.Texture;
			base.Sampler = image.Sampler;
			base.Blend = image.Blend;
			base.Glint = image.Glint;
			const auto& slice = image.Slice;
			const bool sliced = slice.Left > 0.0f || slice.Top > 0.0f || slice.Right > 0.0f || slice.Bottom > 0.0f;

			if (!sliced)
			{
				base.Bounds = dest;
				base.Uv = image.Uv;
				emit(base);
				return;
			}

			const float sx =
				slice.Left + slice.Right > dest.Width && slice.Left + slice.Right > 0.0f ? dest.Width / (slice.Left + slice.Right) : 1.0f;
			const float sy =
				slice.Top + slice.Bottom > dest.Height && slice.Top + slice.Bottom > 0.0f ? dest.Height / (slice.Top + slice.Bottom) : 1.0f;
			const float xs[4] = { dest.X, dest.X + slice.Left * sx, dest.X + dest.Width - slice.Right * sx, dest.X + dest.Width };
			const float ys[4] = { dest.Y, dest.Y + slice.Top * sy, dest.Y + dest.Height - slice.Bottom * sy, dest.Y + dest.Height };
			const auto& uv = image.Uv;
			const float us[4] = { uv.X, uv.X + image.SliceUv.Left, uv.X + uv.Width - image.SliceUv.Right, uv.X + uv.Width };
			const float vs[4] = { uv.Y, uv.Y + image.SliceUv.Top, uv.Y + uv.Height - image.SliceUv.Bottom, uv.Y + uv.Height };

			for (int row = 0; row < 3; ++row)
			{
				for (int column = 0; column < 3; ++column)
				{
					auto quad = base;
					quad.Bounds = { xs[column], ys[row], xs[column + 1] - xs[column], ys[row + 1] - ys[row] };
					quad.Uv = { us[column], vs[row], us[column + 1] - us[column], vs[row + 1] - vs[row] };

					if (quad.Bounds.Width > 0.0f && quad.Bounds.Height > 0.0f)
					{
						emit(quad);
					}
				}
			}
		}

	} // namespace

	namespace
	{

		template <typename T> const T* SpanAt(const std::vector<T>& spans, std::uint32_t offset)
		{
			const auto after = std::upper_bound(spans.begin(), spans.end(), offset,
				[](std::uint32_t value, const T& span)
				{
					return value < span.Begin;
				});
			return after == spans.begin() ? nullptr : &*std::prev(after);
		}

		// Premultiplied colour times a straight-alpha multiplier.
		UiColor Multiply(const UiColor& premultiplied, const UiColor& straight)
		{
			return { premultiplied.R * straight.R * straight.A, premultiplied.G * straight.G * straight.A,
				premultiplied.B * straight.B * straight.A, premultiplied.A * straight.A };
		}

		std::uint32_t Hash(std::uint32_t a, std::uint32_t b)
		{
			std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			return h;
		}

	} // namespace

	template <typename Emit>
	void UiDocument::Impl::PaintText(const Node& node, Text::GlyphAtlas& atlas, const Text::TextLayout& layout, UiPoint origin,
		const UiRect& clip, float opacity, Emit& emit)
	{
		const auto& v = node.Visual;
		const auto& options = node.TextOptions;
		const UiColor text = Fade(Premultiply(v.TextColor), opacity);
		const auto& styles = options.Styles;
		const auto snap = [&](float value)
		{
			return std::round(value * Dpi) / Dpi;
		};
		// Per-face metrics, computed on first use.
		std::array<Text::FontStyleMetrics, 8> metricCache{};
		std::uint32_t metricKnown = 0;
		const auto metricsOf = [&](std::uint32_t face) -> Text::FontStyleMetrics
		{
			if (face < metricCache.size())
			{
				if ((metricKnown & (1u << face)) == 0)
				{
					metricCache[face] = node.Fonts->GetFace(face)->GetStyleMetrics(node.FontSize);
					metricKnown |= 1u << face;
				}

				return metricCache[face];
			}

			return node.Fonts->GetFace(face)->GetStyleMetrics(node.FontSize);
		};
		const bool shadows = std::any_of(styles.begin(), styles.end(),
			[](const UiTextStyleSpan& span)
			{
				return HasFormat(span.Format, UiTextFormat::Shadow);
			});
		// Underline and strikethrough segments merge across neighbouring glyphs.
		struct Line
		{
			UiRect Rect;
			UiColor Color;
			bool Open = false;
		};
		Line lines[2];
		const auto flush = [&](Line& line)
		{
			if (line.Open)
			{
				emit(SolidQuad(node.Id, line.Rect, clip, line.Color));
				line.Open = false;
			}
		};
		const auto addLine = [&](Line& line, const UiRect& rect, const UiColor& color)
		{
			const bool joins = line.Open && line.Color == color && line.Rect.Y == rect.Y && line.Rect.Height == rect.Height &&
				std::abs(line.Rect.X + line.Rect.Width - rect.X) < 1e-3f;

			if (joins)
			{
				line.Rect.Width = rect.X + rect.Width - line.Rect.X;
				return;
			}

			flush(line);
			line = { rect, color, true };
		};

		for (int pass = shadows ? 0 : 1; pass < 2; ++pass)
		{
			const bool shadowPass = pass == 0;

			for (const auto& glyph : layout.GetGlyphs())
			{
				const auto* style = styles.empty() ? nullptr : SpanAt(styles, glyph.Cluster);
				const UiTextFormat format = style ? style->Format : UiTextFormat::None;

				if (shadowPass && !HasFormat(format, UiTextFormat::Shadow))
				{
					continue;
				}

				UiColor color = text;

				if (const auto* span = options.Colors.empty() ? nullptr : SpanAt(options.Colors, glyph.Cluster))
				{
					UiColor straight = span->Color;
					straight.A *= v.TextColor.A;
					color = Fade(Premultiply(straight), opacity);
				}

				if (style)
				{
					color = Multiply(color, style->Color);
				}

				const auto& face = node.Fonts->GetFace(glyph.Face);
				const auto metrics = format == UiTextFormat::None ? Text::FontStyleMetrics{} : metricsOf(glyph.Face);
				UiPoint pen{ origin.X + glyph.X, origin.Y + glyph.Y };

				if (shadowPass)
				{
					const auto shadowMetrics = metricsOf(glyph.Face);
					pen.X += options.Shadow.Offset.X * shadowMetrics.Pixel;
					pen.Y += options.Shadow.Offset.Y * shadowMetrics.Pixel;
					color = options.Shadow.MultiplyTextColor ? Multiply(color, options.Shadow.Color)
															 : Fade(Premultiply(options.Shadow.Color), color.A);
				}

				if (face->IsBitmap() || options.PixelSnap)
				{
					pen = { snap(pen.X), snap(pen.Y) };
				}

				std::uint32_t id = glyph.Glyph;

				if (HasFormat(format, UiTextFormat::Obfuscated))
				{
					id = face->GetSubstituteGlyph(id, Hash(PaintFrame, glyph.Cluster));
				}

				const auto entry = atlas.Get(face, id);

				if (entry.Page != Text::NoAtlasPage)
				{
					auto quad = GlyphQuad(node.Id, entry, atlas.GetDesc(), pen, face->GetRenderSize(node.FontSize), clip, color);

					if (HasFormat(format, UiTextFormat::Italic))
					{
						quad.Shear = metrics.ItalicShear;
						quad.ShearPivotY = pen.Y;
					}

					emit(quad);

					if (HasFormat(format, UiTextFormat::Bold))
					{
						quad.Bounds.X += metrics.BoldOffset;
						emit(quad);
					}
				}

				const UiTextFormat decorations[2] = { UiTextFormat::Underline, UiTextFormat::Strikethrough };

				for (int d = 0; d < 2; ++d)
				{
					if (HasFormat(format, decorations[d]))
					{
						const float top = pen.Y + (d == 0 ? metrics.UnderlineTop : metrics.StrikethroughTop);
						addLine(lines[d], { pen.X, top, glyph.Advance, metrics.LineThickness }, color);
					}
					else
					{
						flush(lines[d]);
					}
				}
			}

			flush(lines[0]);
			flush(lines[1]);
		}
	}

	void UiDocument::Impl::BuildPaint(Node& node, Text::GlyphAtlas& atlas)
	{
		node.Paint.clear();
		const auto emit = [&](const UiPaintQuad& quad)
		{
			if (Visible(quad))
			{
				node.Paint.push_back(quad);
			}
		};
		const auto& s = node.Style;
		const auto& v = node.Visual;
		const float opacity = node.EffectiveOpacity;

		if (opacity <= 0.0f)
		{
			return;
		}

		const auto color = [&](const UiColor& straight)
		{
			return Fade(Premultiply(straight), opacity);
		};
		auto background = SolidQuad(node.Id, node.Bounds, node.Clip, color(v.Background));
		background.CornerRadius = std::min(v.CornerRadius, 0.5f * std::min(node.Bounds.Width, node.Bounds.Height));
		background.BorderWidth = v.BorderWidth;
		background.BorderColor = color(v.BorderColor);

		if (s.Gradient != UiGradient::None)
		{
			background.Gradient = s.Gradient;
			background.ColorEnd = color(s.BackgroundEnd);
		}

		emit(background);

		const UiRect content = Internal::ContentBox(node.Bounds, s.Padding);
		const UiRect clip = s.Clip ? Intersect(node.Clip, content) : node.Clip;

		if (clip.Width <= 0.0f || clip.Height <= 0.0f)
		{
			return;
		}

		const UiPoint origin{ content.X - node.Scroll.X, content.Y - node.Scroll.Y };

		if (v.HasImage)
		{
			AddImage(
				node.Id, v.Image, color(v.ImageTint), FitImage(v.Image, { origin.X, origin.Y, content.Width, content.Height }), clip, emit);
		}

		if (!node.TextLayout)
		{
			return;
		}

		const auto& layout = *node.TextLayout;
		const bool focusedEditor = node.Editable && node.Id == Focused;
		const bool composing = IsComposing(node);
		const auto selectionBegin = std::min(node.Selection.Anchor, node.Selection.Caret);
		const auto selectionEnd = std::max(node.Selection.Anchor, node.Selection.Caret);

		if (focusedEditor && !composing && selectionBegin != selectionEnd)
		{
			for (const auto& rect : layout.GetSelectionRects(selectionBegin, selectionEnd))
			{
				emit(SolidQuad(node.Id, { origin.X + rect.X, origin.Y + rect.Y, rect.Width, rect.Height }, clip, color(s.SelectionColor)));
			}
		}

		if (v.TextColor.A > 0.0f)
		{
			PaintText(node, atlas, layout, origin, clip, opacity, emit);
		}

		if (composing)
		{
			// Underline the preedit text.
			const auto begin = node.Selection.Caret;
			const auto end = begin + static_cast<std::uint32_t>(Composition.size());
			const float thickness = std::max(1.0f, node.FontSize / 16.0f);

			for (const auto& rect : layout.GetSelectionRects(begin, end))
			{
				emit(SolidQuad(node.Id, { origin.X + rect.X, origin.Y + rect.Y + rect.Height - thickness, rect.Width, thickness }, clip,
					color(v.TextColor)));
			}
		}

		if (focusedEditor && CaretVisible && (composing || selectionBegin == selectionEnd))
		{
			const std::uint32_t offset = composing
				? node.Selection.Caret + std::min<std::uint32_t>(CompositionCursor, static_cast<std::uint32_t>(Composition.size()))
				: node.Selection.Caret;
			const auto caret = layout.GetCaret(offset);
			const float width = std::max(1.0f, 1.0f / Dpi);
			emit(SolidQuad(
				node.Id, { origin.X + caret.X - width * 0.5f, origin.Y + caret.Top, width, caret.Height }, clip, color(s.CaretColor)));
		}
	}

	std::size_t UiDocument::PrewarmGlyphs(Text::GlyphAtlas& atlas, const Text::GlyphAtlas::ParallelFor& parallelFor)
	{
		impl->RequireLayout();
		// Unique glyphs per face, in first-use order (deterministic packing).
		std::vector<std::pair<std::shared_ptr<const Text::FontFace>, std::vector<std::uint32_t>>> perFace;

		for (const auto id : impl->Order)
		{
			const auto& node = impl->Get(id);

			if (!node.TextLayout || (node.Style.TextColor.A <= 0.0f && node.Visual.TextColor.A <= 0.0f))
			{
				continue;
			}

			for (const auto& glyph : node.TextLayout->GetGlyphs())
			{
				const auto& face = node.Fonts->GetFace(glyph.Face);
				auto entry = std::find_if(perFace.begin(), perFace.end(),
					[&](const auto& candidate)
					{
						return candidate.first == face;
					});

				if (entry == perFace.end())
				{
					perFace.push_back({ face, {} });
					entry = std::prev(perFace.end());
				}

				if (!atlas.Contains(*face, glyph.Glyph))
				{
					entry->second.push_back(glyph.Glyph);
				}
			}
		}

		std::size_t added = 0;

		for (const auto& [face, glyphs] : perFace)
		{
			added += atlas.Prewarm(face, glyphs, parallelFor);
		}

		return added;
	}

	const std::vector<UiPaintQuad>& UiDocument::Paint(Text::GlyphAtlas& atlas)
	{
		impl->RequireLayout();

		if (impl->PaintAtlas != &atlas)
		{
			for (auto& [id, node] : impl->Nodes)
			{
				node.PaintDirty = true;
			}

			impl->PaintAtlas = &atlas;
		}

		impl->ResolveVisuals();
		++impl->PaintFrame;
		const auto previousCount = impl->Quads.size();
		impl->Quads.clear();
		impl->RepaintedNodes = 0;

		for (const auto id : impl->Order)
		{
			auto& node = impl->Get(id);
			// Opacity multiplies down the tree; hidden scroll bars hide their parts.
			float opacity = node.ControlHidden ? 0.0f : node.Visual.Opacity * node.ControlOpacity;

			if (node.Parent && impl->Nodes.contains(node.Parent.Value))
			{
				opacity *= impl->Get(node.Parent).EffectiveOpacity;
			}

			node.EffectiveOpacity = opacity;
			const bool moved = !Internal::SameRect(node.PaintedBounds, node.Bounds) || !Internal::SameRect(node.PaintedClip, node.Clip) ||
				node.PaintedScroll.X != node.Scroll.X || node.PaintedScroll.Y != node.Scroll.Y || node.PaintedOpacity != opacity;

			if (node.PaintDirty || moved || (node.Obfuscated && node.TextLayout))
			{
				impl->BuildPaint(node, atlas);
				node.PaintDirty = false;
				node.PaintedBounds = node.Bounds;
				node.PaintedClip = node.Clip;
				node.PaintedScroll = node.Scroll;
				node.PaintedOpacity = opacity;
				++impl->RepaintedNodes;
			}

			impl->Quads.insert(impl->Quads.end(), node.Paint.begin(), node.Paint.end());
		}

		if (impl->RepaintedNodes > 0 || impl->Quads.size() != previousCount || impl->PaintedLayoutRevision != impl->Revision)
		{
			++impl->PaintRevision;
			impl->PaintedLayoutRevision = impl->Revision;
		}

		return impl->Quads;
	}

} // namespace Swim::UI
