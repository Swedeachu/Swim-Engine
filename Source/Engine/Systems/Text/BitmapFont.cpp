#include "Engine/Systems/Text/Internal/BitmapFace.h"

#include "Engine/Systems/Text/FontCollection.h"
#include "Engine/Systems/Text/Utf8.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Text
{

	namespace
	{

		bool Finite(float value, float low, float high)
		{
			return std::isfinite(value) && value >= low && value <= high;
		}

		// Last opaque column + 1 inside a rectangle (0 when fully transparent).
		std::uint32_t OpaqueWidth(const FontFace::BitmapFace::Page& page, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h)
		{
			for (std::uint32_t column = w; column > 0; --column)
			{
				for (std::uint32_t row = 0; row < h; ++row)
				{
					if (page.Rgba[((std::size_t(y) + row) * page.Width + x + column - 1) * 4 + 3] != 0)
					{
						return column;
					}
				}
			}

			return 0;
		}

		const FontFace::BitmapFace::Block EmptyBlock{};

	} // namespace

	FontFace::BitmapFace::BitmapFace(BitmapFontDesc description)
		: Desc(std::move(description)), Blocks(std::make_unique<std::atomic<const Block*>[]>(BlockCount))
	{
		const auto& d = Desc;
		const bool valid = d.CellsPerRow <= 256 && Finite(d.CellPixels, 0.01f, 4096.0f) && Finite(d.EmPixels, 0.01f, 4096.0f) &&
			Finite(d.Ascent, 0.0f, 4096.0f) && Finite(d.Descent, 0.0f, 4096.0f) && Finite(d.LineHeight, 0.0f, 4096.0f) &&
			Finite(d.Baseline, -4096.0f, 4096.0f) && Finite(d.Spacing, -4096.0f, 4096.0f) && Finite(d.SpaceAdvance, 0.0f, 4096.0f) &&
			Finite(d.BoldOffset, 0.0f, 4096.0f) && Finite(d.ItalicShear, -4.0f, 4.0f) && Finite(d.UnderlineOffset, -4096.0f, 4096.0f) &&
			Finite(d.StrikethroughOffset, -4096.0f, 4096.0f) && Finite(d.LineThickness, 0.0f, 4096.0f) && d.LoadPage;

		if (!valid)
		{
			throw std::invalid_argument("Invalid bitmap font metrics (or no LoadPage)");
		}

		for (const auto& [codePoint, glyph] : d.Glyphs)
		{
			if (codePoint == 0 || codePoint > 0x10FFFF || (glyph.Advance && !Finite(*glyph.Advance, 0.0f, 4096.0f)) ||
				(glyph.BaselineOffset && !Finite(*glyph.BaselineOffset, -4096.0f, 4096.0f)))
			{
				throw std::invalid_argument("Invalid explicit bitmap glyph");
			}

			ExplicitByBlock[std::uint32_t(codePoint) >> 8].emplace_back(codePoint, glyph);
		}
	}

	float FontFace::BitmapFace::PixelUnits(float size) const
	{
		const float units = size / Desc.EmPixels;
		return Desc.IntegerScale ? std::max(1.0f, std::floor(units + 1e-4f)) : units;
	}

	const FontFace::BitmapFace::Page* FontFace::BitmapFace::LoadPage(std::uint32_t id) const
	{
		if (const auto found = Pages.find(id); found != Pages.end())
		{
			return found->second.get();
		}

		std::unique_ptr<Page> page;

		if (auto loaded = Desc.LoadPage(id))
		{
			const std::size_t bytes = std::size_t(loaded->Width) * loaded->Height * 4;

			if (loaded->Width == 0 || loaded->Height == 0 || loaded->Width > 16384 || loaded->Height > 16384 || loaded->Rgba.size() != bytes ||
				!Finite(loaded->FontPixelsPerTexel, 0.0f, 4096.0f))
			{
				throw std::invalid_argument("Invalid bitmap font page " + std::to_string(id));
			}

			page = std::make_unique<Page>();
			page->Width = loaded->Width;
			page->Height = loaded->Height;
			page->Rgba = std::move(loaded->Rgba);
			page->FontPixelsPerTexel = loaded->FontPixelsPerTexel > 0.0f ? loaded->FontPixelsPerTexel
				: Desc.CellsPerRow > 0											  ? Desc.CellPixels * float(Desc.CellsPerRow) / float(page->Width)
																				  : 1.0f;
		}

		return Pages.emplace(id, std::move(page)).first->second.get();
	}

	const FontFace::BitmapFace::Block* FontFace::BitmapFace::Load(std::uint32_t block) const
	{
		if (const auto* ready = Blocks[block].load(std::memory_order_acquire))
		{
			return ready;
		}

		std::lock_guard lock(Mutex);

		if (const auto* ready = Blocks[block].load(std::memory_order_relaxed))
		{
			return ready;
		}

		const auto explicitGlyphs = ExplicitByBlock.find(block);
		const Page* grid = Desc.CellsPerRow > 0 ? LoadPage(block) : nullptr;

		if (!grid && explicitGlyphs == ExplicitByBlock.end() && block != 0)
		{
			Blocks[block].store(&EmptyBlock, std::memory_order_release);
			return &EmptyBlock;
		}

		auto result = std::make_unique<Block>();

		if (grid)
		{
			const std::uint32_t cellWidth = grid->Width / Desc.CellsPerRow;
			const std::uint32_t cellHeight = grid->Height / Desc.CellsPerRow;

			for (std::uint32_t index = 0; index < 256 && cellWidth > 0 && cellHeight > 0; ++index)
			{
				const std::uint32_t row = index / Desc.CellsPerRow;

				if (row >= Desc.CellsPerRow)
				{
					break;
				}

				auto& glyph = result->Glyphs[index];
				glyph.X = (index % Desc.CellsPerRow) * cellWidth;
				glyph.Y = row * cellHeight;
				glyph.Height = cellHeight;
				glyph.Width = OpaqueWidth(*grid, glyph.X, glyph.Y, cellWidth, cellHeight);
				glyph.TexelScale = grid->FontPixelsPerTexel;
				glyph.Top = Desc.Baseline;

				if (glyph.Width > 0)
				{
					glyph.Source = grid;
					glyph.Exists = true;
					glyph.Advance = float(glyph.Width) * glyph.TexelScale + Desc.Spacing;
				}
			}
		}

		if (explicitGlyphs != ExplicitByBlock.end())
		{
			for (const auto& [codePoint, desc] : explicitGlyphs->second)
			{
				const Page* page = LoadPage(desc.Page);

				if (!page || desc.X + desc.Width > page->Width || desc.Y + desc.Height > page->Height)
				{
					throw std::invalid_argument("Explicit bitmap glyph lies outside its page");
				}

				auto& glyph = result->Glyphs[std::uint32_t(codePoint) & 0xFFu];
				glyph = {};
				glyph.X = desc.X;
				glyph.Y = desc.Y;
				glyph.Height = desc.Height;
				glyph.Width = OpaqueWidth(*page, desc.X, desc.Y, desc.Width, desc.Height);
				glyph.TexelScale = page->FontPixelsPerTexel;
				glyph.Top = desc.BaselineOffset.value_or(Desc.Baseline);
				glyph.Source = glyph.Width > 0 ? page : nullptr;
				glyph.Advance = desc.Advance.value_or(glyph.Width > 0 ? float(glyph.Width) * glyph.TexelScale + Desc.Spacing : Desc.SpaceAdvance);
				glyph.Exists = true;
			}
		}

		auto& space = result->Glyphs[0x20];

		if (block == 0 && !space.Exists)
		{
			space = {};
			space.Exists = true;
			space.Advance = Desc.SpaceAdvance;
		}

		const Block* published = result.get();
		Store.push_back(std::move(result));
		Blocks[block].store(published, std::memory_order_release);
		return published;
	}

	const FontFace::BitmapFace::Glyph* FontFace::BitmapFace::Find(char32_t codePoint) const
	{
		if (codePoint == 0 || codePoint > 0x10FFFF)
		{
			return nullptr;
		}

		const auto& glyph = Load(std::uint32_t(codePoint) >> 8)->Glyphs[std::uint32_t(codePoint) & 0xFFu];
		return glyph.Exists ? &glyph : nullptr;
	}

	ShapedRun ShapeBitmap(const FontFace::BitmapFace& face, std::string_view utf8, float size, const ShapeOptions& options)
	{
		ShapedRun result;
		const float unit = face.PixelUnits(size);
		result.Metrics = { face.Desc.Ascent * unit, -face.Desc.Descent * unit, face.Desc.LineHeight * unit };
		result.Direction = options.Direction == TextDirection::RightToLeft ? TextDirection::RightToLeft : TextDirection::LeftToRight;
		result.Glyphs.reserve(utf8.size());

		for (std::size_t offset = 0; offset < utf8.size();)
		{
			const auto decoded = DecodeUtf8(utf8, offset);
			ShapedGlyph glyph;
			glyph.Cluster = std::uint32_t(offset);

			if (const auto* found = face.Find(decoded.CodePoint))
			{
				glyph.Glyph = std::uint32_t(decoded.CodePoint);
				glyph.AdvanceX = found->Advance * unit;
			}
			else if (!FontCollection::IsIgnorable(decoded.CodePoint))
			{
				glyph.AdvanceX = face.Desc.SpaceAdvance * unit; // .notdef: blank, but it takes room.
				++result.MissingGlyphs;
			}

			result.AdvanceX += glyph.AdvanceX;
			result.Glyphs.push_back(glyph);
			offset += decoded.Length;
		}

		if (result.Direction == TextDirection::RightToLeft)
		{
			std::reverse(result.Glyphs.begin(), result.Glyphs.end());
		}

		return result;
	}

} // namespace Swim::Text
