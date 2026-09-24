#include "Engine/Systems/Text/GlyphAtlas.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Text
{
	GlyphAtlas::GlyphAtlas(const GlyphAtlasDesc& description) : desc(description)
	{
		if (desc.PageSize < 16 || desc.PageSize > 4096 || desc.MaxPages == 0 || desc.MaxPages > 256 || !std::isfinite(desc.EmSize) ||
			desc.EmSize < 4.0f || desc.EmSize > 512.0f || !std::isfinite(desc.DistanceRange) || desc.DistanceRange < 1.0f ||
			desc.DistanceRange > 32.0f ||
			static_cast<std::uint64_t>(desc.PageSize) * desc.PageSize * 3 * desc.MaxPages > 256u * 1024u * 1024u)
		{
			throw std::invalid_argument("Invalid glyph atlas configuration (256 MiB maximum)");
		}
	}

	std::size_t GlyphAtlas::KeyHash::operator()(const Key& key) const
	{
		return std::hash<const FontFace*>{}(key.Face) ^ (std::hash<std::uint32_t>{}(key.Glyph) << 1);
	}

	AtlasGlyph GlyphAtlas::Get(const std::shared_ptr<const FontFace>& face, std::uint32_t glyph)
	{
		if (!face)
		{
			throw std::invalid_argument("Glyph atlas needs a font face");
		}
		const Key key{ face.get(), glyph };
		if (const auto found = glyphs.find(key); found != glyphs.end())
		{
			return found->second.Glyph;
		}
		const auto bitmap = face->Rasterize(glyph, desc.EmSize, desc.DistanceRange, desc.PageSize - 2);
		AtlasGlyph result;
		result.Width = bitmap.Width;
		result.Height = bitmap.Height;
		result.Left = bitmap.Left / desc.EmSize;
		result.Top = bitmap.Top / desc.EmSize;
		result.WidthEm = bitmap.Width / desc.EmSize;
		result.HeightEm = bitmap.Height / desc.EmSize;
		if (bitmap.Width == 0 || bitmap.Height == 0)
		{
			glyphs.emplace(key, Entry{ face, result });
			return result;
		}
		std::uint32_t pageIndex = 0;
		std::uint32_t x = 1;
		std::uint32_t y = 1;
		std::uint32_t rowHeight = 0;
		for (; pageIndex < pages.size(); ++pageIndex)
		{
			const auto& page = pages[pageIndex];
			x = page.X;
			y = page.Y;
			rowHeight = page.RowHeight;
			if (x + bitmap.Width + 1 > desc.PageSize)
			{
				x = 1;
				y += rowHeight + 1;
				rowHeight = 0;
			}
			if (y + bitmap.Height + 1 <= desc.PageSize)
			{
				break;
			}
		}
		const bool newPage = pageIndex == pages.size();
		if (newPage)
		{
			if (pages.size() == desc.MaxPages)
			{
				throw std::length_error("Glyph atlas page budget exhausted");
			}
			x = y = 1;
			rowHeight = 0;
			Page page;
			page.Pixels.resize(static_cast<std::size_t>(desc.PageSize) * desc.PageSize * 3, 0);
			pages.push_back(std::move(page));
		}
		result.Page = pageIndex;
		result.X = x;
		result.Y = y;
		try
		{
			// Insert before committing the shelf or writing texels; allocation failure
			// leaves all existing entries, revisions and packing positions unchanged.
			glyphs.emplace(key, Entry{ face, result });
		}
		catch (...)
		{
			if (newPage)
			{
				pages.pop_back();
			}
			throw;
		}
		auto& page = pages[pageIndex];
		for (std::uint32_t row = 0; row < bitmap.Height; ++row)
		{
			std::copy_n(bitmap.Pixels.data() + static_cast<std::size_t>(row) * bitmap.Width * 3, bitmap.Width * 3,
				page.Pixels.data() + (static_cast<std::size_t>(y + row) * desc.PageSize + x) * 3);
		}
		page.X = x + bitmap.Width + 1;
		page.Y = y;
		page.RowHeight = std::max(rowHeight, bitmap.Height);
		++page.Revision;
		return result;
	}

	AtlasPageView GlyphAtlas::GetPage(std::uint32_t index) const
	{
		const auto& page = pages.at(index);
		return { desc.PageSize, page.Revision, page.Pixels };
	}
} // namespace Swim::Text
