#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace Swim::Text;
using Swim::Testing::LoadTextFontFixture;

namespace
{
	std::uint8_t Median(const std::uint8_t* p)
	{
		return std::max(std::min(p[0], p[1]), std::min(std::max(p[0], p[1]), p[2]));
	}
} // namespace

SWIM_TEST("Text.Atlas", "CachesGlyphsAndKeepsPagesStableAcrossGrowth")
{
	const auto font = LoadTextFontFixture();
	GlyphAtlas atlas({ 64, 16, 24, 4 });
	const auto a = atlas.Get(font, font->GetGlyph(U'A'));
	SWIM_REQUIRE_EQUAL(a.Page, 0u);
	const auto before = atlas.GetPage(0);
	const auto revision = before.Revision;
	const std::vector<std::uint8_t> pixels(before.Pixels.begin(), before.Pixels.end());
	const auto again = atlas.Get(font, font->GetGlyph(U'A'));
	SWIM_CHECK_EQUAL(again.X, a.X);
	SWIM_CHECK_EQUAL(again.Y, a.Y);
	SWIM_CHECK_EQUAL(atlas.GetPage(0).Revision, revision);
	const auto space = atlas.Get(font, font->GetGlyph(U' '));
	SWIM_CHECK_EQUAL(space.Page, NoAtlasPage);
	SWIM_CHECK_EQUAL(atlas.GetPage(0).Revision, revision);
	for (char32_t c = U'B'; c <= U'Z'; ++c)
	{
		atlas.Get(font, font->GetGlyph(c));
	}
	SWIM_CHECK(atlas.GetPageCount() > 1);
	const auto after = atlas.GetPage(0);
	for (std::uint32_t y = a.Y; y < a.Y + a.Height; ++y)
	{
		for (std::uint32_t x = a.X; x < a.X + a.Width; ++x)
		{
			for (std::uint32_t c = 0; c < 3; ++c)
			{
				const auto i = (y * after.Size + x) * 3 + c;
				SWIM_CHECK_EQUAL(after.Pixels[i], pixels[i]);
			}
		}
	}
	SWIM_CHECK_THROWS(atlas.GetPage(atlas.GetPageCount()), std::out_of_range);
}

SWIM_TEST("Text.Atlas", "BudgetFailuresDoNotCorruptCachedGlyphs")
{
	const auto font = LoadTextFontFixture();
	GlyphAtlas atlas({ 32, 1, 20, 2 });
	const auto first = atlas.Get(font, font->GetGlyph(U'W'));
	bool full = false;
	for (char32_t c = U'A'; c < U'Z'; ++c)
	{
		try
		{
			atlas.Get(font, font->GetGlyph(c));
		}
		catch (const std::length_error&)
		{
			full = true;
			break;
		}
	}
	SWIM_REQUIRE(full);
	SWIM_CHECK_EQUAL(atlas.GetPageCount(), 1u);
	const auto current = atlas.Get(font, font->GetGlyph(U'W'));
	SWIM_CHECK_EQUAL(current.Page, first.Page);
	SWIM_CHECK_EQUAL(current.X, first.X);
	const auto revision = atlas.GetPage(0).Revision;
	SWIM_CHECK_THROWS(atlas.Get(font, std::numeric_limits<std::uint32_t>::max()), std::out_of_range);
	SWIM_CHECK_EQUAL(atlas.GetPage(0).Revision, revision);
	GlyphAtlas tooSmall({ 16, 1, 48, 4 });
	SWIM_CHECK_THROWS(tooSmall.Get(font, font->GetGlyph(U'W')), std::length_error);
	SWIM_CHECK_EQUAL(tooSmall.GetPageCount(), 0u);
	SWIM_CHECK_EQUAL(tooSmall.GetGlyphCount(), 0u);
	SWIM_CHECK_THROWS(atlas.Get({}, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(GlyphAtlas(GlyphAtlasDesc{ 4096, 256, 48, 4 }), std::invalid_argument);
}

SWIM_TEST("Text.Atlas", "RetainsFontIdentityAfterCallerReleasesIt")
{
	GlyphAtlas atlas;
	auto first = LoadTextFontFixture();
	const std::weak_ptr<const FontFace> weak = first;
	atlas.Get(first, first->GetGlyph(U'A'));
	first.reset();
	SWIM_CHECK(!weak.expired());
	const auto second = LoadTextFontFixture();
	atlas.Get(second, second->GetGlyph(U'A'));
	SWIM_CHECK_EQUAL(atlas.GetGlyphCount(), 2u);
}

SWIM_TEST("Text.Atlas", "DistanceSignsAndTopDownOrientationMatchFreeTypeCoverage")
{
	const auto font = LoadTextFontFixture();
	GlyphAtlas atlas;
	FT_Library library = nullptr;
	SWIM_REQUIRE_EQUAL(FT_Init_FreeType(&library), 0);
	FT_Face reference = nullptr;
	const auto loadError = FT_New_Face(library, SWIM_TEXT_FONT_FIXTURE_PATH, 0, &reference);
	if (loadError != 0)
	{
		FT_Done_FreeType(library);
		SWIM_FAIL("Cannot load reference font");
		return;
	}
	FT_Set_Pixel_Sizes(reference, 0, 48);
	std::size_t compared = 0;
	std::size_t mismatches = 0;
	for (const char32_t c : { U'L', U'O', U'A', U'g' })
	{
		const auto glyph = atlas.Get(font, font->GetGlyph(c));
		const auto page = atlas.GetPage(glyph.Page);
		const int error = FT_Load_Char(reference, c, FT_LOAD_NO_HINTING | FT_LOAD_RENDER);
		SWIM_CHECK_EQUAL(error, 0);
		if (error != 0)
		{
			continue;
		}
		const auto& bitmap = reference->glyph->bitmap;
		for (unsigned y = 0; y < bitmap.rows; ++y)
		{
			for (unsigned x = 0; x < bitmap.width; ++x)
			{
				const auto coverage = bitmap.buffer[y * bitmap.pitch + x];
				if (coverage > 16 && coverage < 239)
				{
					continue; // Ignore antialiasing at the contour.
				}
				const int gx = static_cast<int>(x) + reference->glyph->bitmap_left - static_cast<int>(std::lround(glyph.Left * 48));
				const int gy = static_cast<int>(y) - reference->glyph->bitmap_top + static_cast<int>(std::lround(glyph.Top * 48));
				if (gx < 0 || gy < 0 || gx >= static_cast<int>(glyph.Width) || gy >= static_cast<int>(glyph.Height))
				{
					++mismatches;
					continue;
				}
				const auto* pixel = page.Pixels.data() + ((glyph.Y + gy) * page.Size + glyph.X + gx) * 3;
				mismatches += (Median(pixel) > 127) != (coverage > 127) ? 1u : 0u;
				++compared;
			}
		}
	}
	FT_Done_Face(reference);
	FT_Done_FreeType(library);
	SWIM_CHECK(compared > 1500);
	SWIM_CHECK_MESSAGE(mismatches * 100 < compared, "MSDF sign/orientation must agree with over 99% of non-edge FreeType samples");
}

SWIM_TEST("Text.Atlas", "ChangedRowsCoverEveryWriteSinceARevision")
{
	const auto font = LoadTextFontFixture();
	GlyphAtlas atlas({ 128, 4, 24, 4 });
	const auto first = atlas.Get(font, font->GetGlyph(U'H'));
	SWIM_REQUIRE_EQUAL(first.Page, 0u);
	SWIM_CHECK(atlas.GetChangedRows(0, 0) == (AtlasRowSpan{ first.Y, first.Height }));
	SWIM_CHECK(atlas.GetChangedRows(0, 1) == AtlasRowSpan{});

	// Fill the first shelf and start the next: a consumer at revision 1 needs both bands.
	std::uint32_t lowest = first.Y;
	std::uint32_t highest = first.Y + first.Height;
	for (char32_t c = U'a'; c <= U'z'; ++c)
	{
		const auto glyph = atlas.Get(font, font->GetGlyph(c));
		if (glyph.Page == 0)
		{
			lowest = std::min(lowest, glyph.Y);
			highest = std::max(highest, glyph.Y + glyph.Height);
		}
	}
	const auto page = atlas.GetPage(0);
	SWIM_CHECK(page.Revision > 2);
	const auto since = atlas.GetChangedRows(0, 1);
	SWIM_CHECK(since.Y <= lowest);
	SWIM_CHECK(since.Y + since.Height >= highest);
	SWIM_CHECK(since.Y + since.Height <= page.Size);
	SWIM_CHECK(atlas.GetChangedRows(0, page.Revision) == AtlasRowSpan{});
	SWIM_CHECK_THROWS(atlas.GetChangedRows(0, page.Revision + 1), std::out_of_range);
	SWIM_CHECK_THROWS(atlas.GetChangedRows(atlas.GetPageCount(), 0), std::out_of_range);
	// Cache hits and whitespace change nothing.
	atlas.Get(font, font->GetGlyph(U'H'));
	atlas.Get(font, font->GetGlyph(U' '));
	SWIM_CHECK_EQUAL(atlas.GetPage(0).Revision, page.Revision);
}
