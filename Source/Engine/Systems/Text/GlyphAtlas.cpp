#include "Engine/Systems/Text/GlyphAtlas.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>

namespace Swim::Text
{
	namespace
	{
		constexpr std::uint64_t MaxAtlasBytes = 256ull * 1024 * 1024;

		bool InRange(float value, float low, float high)
		{
			return std::isfinite(value) && value >= low && value <= high;
		}
	} // namespace

	GlyphAtlas::GlyphAtlas(const GlyphAtlasDesc& description) : desc(description)
	{
		const std::uint64_t pageBytes = static_cast<std::uint64_t>(desc.PageSize) * desc.PageSize * 3;
		const bool valid = desc.PageSize >= 16 && desc.PageSize <= 4096 && desc.MaxPages >= 1 && desc.MaxPages <= 256 &&
			InRange(desc.EmSize, 4.0f, 512.0f) && InRange(desc.DistanceRange, 1.0f, 32.0f) && pageBytes * desc.MaxPages <= MaxAtlasBytes;
		if (!valid)
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
		return Insert(face, glyph, face->Rasterize(glyph, desc.EmSize, desc.DistanceRange, desc.PageSize - 2));
	}

	bool GlyphAtlas::Contains(const FontFace& face, std::uint32_t glyph) const
	{
		return glyphs.contains(Key{ &face, glyph });
	}

	std::size_t GlyphAtlas::Prewarm(
		const std::shared_ptr<const FontFace>& face, std::span<const std::uint32_t> requested, const ParallelFor& parallelFor)
	{
		if (!face)
		{
			throw std::invalid_argument("Glyph atlas needs a font face");
		}
		std::vector<std::uint32_t> missing;
		for (const auto glyph : requested)
		{
			if (!Contains(*face, glyph) && std::find(missing.begin(), missing.end(), glyph) == missing.end())
			{
				missing.push_back(glyph);
			}
		}
		std::vector<FontFace::GlyphBitmap> bitmaps(missing.size());
		std::exception_ptr failure;
		std::mutex failureMutex;
		const auto rasterize = [&](std::size_t index)
		{
			try
			{
				bitmaps[index] = face->Rasterize(missing[index], desc.EmSize, desc.DistanceRange, desc.PageSize - 2);
			}
			catch (...)
			{
				std::lock_guard lock(failureMutex);
				if (!failure)
				{
					failure = std::current_exception();
				}
			}
		};
		if (parallelFor && missing.size() > 1)
		{
			parallelFor(missing.size(), rasterize);
		}
		else
		{
			for (std::size_t index = 0; index < missing.size(); ++index)
			{
				rasterize(index);
			}
		}
		if (failure)
		{
			std::rethrow_exception(failure);
		}
		for (std::size_t index = 0; index < missing.size(); ++index)
		{
			Insert(face, missing[index], bitmaps[index]);
		}
		return missing.size();
	}

	AtlasGlyph GlyphAtlas::Insert(const std::shared_ptr<const FontFace>& face, std::uint32_t glyph, const FontFace::GlyphBitmap& bitmap)
	{
		const Key key{ face.get(), glyph };
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
			// Allocate before committing the shelf or writing texels; allocation failure
			// leaves all existing entries, revisions and packing positions unchanged.
			pages[pageIndex].Writes.reserve(pages[pageIndex].Writes.size() + 1);
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
		page.Writes.push_back({ y, bitmap.Height });
		++page.Revision;
		return result;
	}

	AtlasPageView GlyphAtlas::GetPage(std::uint32_t index) const
	{
		const auto& page = pages.at(index);
		return { desc.PageSize, page.Revision, page.Pixels };
	}

	AtlasRowSpan GlyphAtlas::GetChangedRows(std::uint32_t index, std::uint64_t sinceRevision) const
	{
		const auto& page = pages.at(index);
		if (sinceRevision > page.Revision)
		{
			throw std::out_of_range("Glyph atlas page has not reached that revision");
		}
		std::uint32_t top = desc.PageSize;
		std::uint32_t bottom = 0;
		for (auto write = page.Writes.begin() + static_cast<std::ptrdiff_t>(sinceRevision); write != page.Writes.end(); ++write)
		{
			top = std::min(top, write->Y);
			bottom = std::max(bottom, write->Y + write->Height);
		}
		return top < bottom ? AtlasRowSpan{ top, bottom - top } : AtlasRowSpan{};
	}
} // namespace Swim::Text
