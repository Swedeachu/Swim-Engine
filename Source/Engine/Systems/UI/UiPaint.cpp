#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <algorithm>

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
			quad.Kind = UiPaintKind::Glyph;
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
			const bool opaque =
				quad.Color.A > 0.0f || (quad.Kind == UiPaintKind::Solid && quad.BorderWidth > 0.0f && quad.BorderColor.A > 0.0f);
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
			const UiColor text = color(v.TextColor);
			for (const auto& glyph : layout.GetGlyphs())
			{
				const auto entry = atlas.Get(node.Fonts->GetFace(glyph.Face), glyph.Glyph);
				if (entry.Page != Text::NoAtlasPage)
				{
					emit(GlyphQuad(node.Id, entry, atlas.GetDesc(), { origin.X + glyph.X, origin.Y + glyph.Y }, node.FontSize, clip, text));
				}
			}
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
			if (node.PaintDirty || moved)
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
