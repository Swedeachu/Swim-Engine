#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/Text/TextLayout.h"
#include "Tests/Fixtures/BitmapFontFixture.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>

using namespace Swim::Text;
using Swim::Testing::MakeBitmapFontFixture;

namespace
{

	float Advance(const FontFace& face, std::string_view text, float size)
	{
		return face.Shape(text, size).AdvanceX;
	}

} // namespace

SWIM_TEST("Text.Bitmap", "AdvancesComeFromOpaqueColumnsPlusSpacing")
{
	const auto face = MakeBitmapFontFixture();
	SWIM_CHECK(face->IsBitmap());
	SWIM_CHECK_EQUAL(face->GetGlyph(U'A'), std::uint32_t(U'A'));
	SWIM_CHECK_EQUAL(face->GetGlyph(U'é'), 0u); // Empty cell: missing.
	SWIM_CHECK_EQUAL(face->GetGlyph(U'一'), 0u); // Absent block.
	SWIM_CHECK_EQUAL(face->GetGlyph(U' '), std::uint32_t(U' '));
	SWIM_CHECK_NEAR(Advance(*face, "A", 8), 6.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "i", 8), 2.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "l", 8), 3.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "|", 8), 3.0f, 1e-6f); // Leading empty column counts.
	SWIM_CHECK_NEAR(Advance(*face, " ", 8), 4.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "Ail A", 8), 6 + 2 + 3 + 4 + 6, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "\xC4\x80", 8), 5.0f, 1e-6f); // U+0100 on block 1.
	// Integer scale: 16 and 23 units per em are both 2 units per font pixel.
	SWIM_CHECK_NEAR(Advance(*face, "A", 16), 12.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "A", 23), 12.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "A", 4), 6.0f, 1e-6f); // Never below one unit per pixel.
	SWIM_CHECK_NEAR(face->GetRenderSize(23), 16.0f, 1e-6f);
	const auto metrics = face->GetMetrics(24);
	SWIM_CHECK_NEAR(metrics.Ascender, 21.0f, 1e-6f);
	SWIM_CHECK_NEAR(metrics.Descender, -3.0f, 1e-6f);
	SWIM_CHECK_NEAR(metrics.LineHeight, 27.0f, 1e-6f);
	const auto style = face->GetStyleMetrics(24);
	SWIM_CHECK_NEAR(style.Pixel, 3.0f, 1e-6f);
	SWIM_CHECK_NEAR(style.BoldOffset, 3.0f, 1e-6f);
}

SWIM_TEST("Text.Bitmap", "HighResolutionPagesKeepTheGridsMetrics")
{
	// 32-texel cells of the same 8-pixel grid: four texels per font pixel.
	const auto face = MakeBitmapFontFixture({}, 0.0f, 32);
	SWIM_CHECK_NEAR(Advance(*face, "A", 8), 6.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "Ail", 16), 22.0f, 1e-6f);
	GlyphAtlas atlas({ 256, 4, 48, 4 });
	const auto a = atlas.Get(face, U'A');
	SWIM_CHECK_EQUAL(a.Width, 20u); // Trimmed to the last opaque column.
	SWIM_CHECK_EQUAL(a.Height, 32u);
	SWIM_CHECK_NEAR(a.WidthEm, 5.0f / 8.0f, 1e-6f);
	SWIM_CHECK_NEAR(a.HeightEm, 1.0f, 1e-6f);
}

SWIM_TEST("Text.Bitmap", "PagesLoadLazilyOncePerBlock")
{
	auto loads = std::make_shared<std::atomic<int>>(0);
	const auto face = MakeBitmapFontFixture(loads);
	SWIM_CHECK_EQUAL(loads->load(), 0);
	face->GetGlyph(U'A');
	face->GetGlyph(U'l');
	Advance(*face, "AiAi", 8);
	SWIM_CHECK_EQUAL(loads->load(), 1);
	face->GetGlyph(U'Ā');
	SWIM_CHECK_EQUAL(loads->load(), 2);
	face->GetGlyph(U'一');
	face->GetGlyph(U'丁');
	SWIM_CHECK_EQUAL(loads->load(), 3); // An absent block is asked once.
}

SWIM_TEST("Text.Bitmap", "ExplicitGlyphsOverrideTheGrid")
{
	BitmapFontDesc desc;
	desc.CellsPerRow = 0;
	desc.LoadPage = [](std::uint32_t id) -> std::optional<BitmapFontPage>
	{
		if (id != 0x2000)
		{
			return std::nullopt;
		}

		BitmapFontPage page;
		page.Width = 16;
		page.Height = 8;
		page.FontPixelsPerTexel = 1.0f;
		page.Rgba.assign(16 * 8 * 4, 255);
		return page;
	};
	BitmapFontGlyph star;
	star.Page = 0x2000;
	star.X = 2;
	star.Width = 9;
	star.Height = 8;
	star.Advance = 10.0f;
	star.BaselineOffset = 6.0f;
	BitmapFontGlyph bar = star;
	bar.Advance.reset();
	bar.Width = 3;
	desc.Glyphs = { { U'*', star }, { U'', bar } };
	const auto face = std::make_shared<FontFace>(desc);
	SWIM_CHECK_NEAR(Advance(*face, "*", 8), 10.0f, 1e-6f);
	SWIM_CHECK_NEAR(Advance(*face, "\xEE\x80\x80", 8), 4.0f, 1e-6f); // 3 columns + spacing.
	SWIM_CHECK_NEAR(Advance(*face, " ", 8), 4.0f, 1e-6f);
	SWIM_CHECK_EQUAL(face->GetGlyph(U'A'), 0u);
	GlyphAtlas atlas({ 64, 4, 48, 4 });
	const auto entry = atlas.Get(face, U'*');
	SWIM_CHECK(entry.Color);
	SWIM_CHECK_NEAR(entry.Top, 6.0f / 8.0f, 1e-6f);
	BitmapFontDesc bad = desc;
	bad.Glyphs[0].second.X = 12;
	const auto broken = std::make_shared<FontFace>(bad);
	SWIM_CHECK_THROWS(broken->GetGlyph(U'*'), std::invalid_argument);
	bad.LoadPage = {};
	SWIM_CHECK_THROWS(FontFace{ bad }, std::invalid_argument);
}

SWIM_TEST("Text.Bitmap", "GlyphsAreCopiedOntoColorAtlasPages")
{
	const auto bitmap = MakeBitmapFontFixture();
	const auto vector = Swim::Testing::LoadTextFontFixture();
	GlyphAtlas atlas({ 64, 4, 24, 4 });
	const auto msdf = atlas.Get(vector, vector->GetGlyph(U'A'));
	const auto a = atlas.Get(bitmap, U'A');
	const auto red = atlas.Get(bitmap, U'Ā');
	SWIM_CHECK(!msdf.Color);
	SWIM_CHECK(a.Color);
	SWIM_CHECK(a.Page != msdf.Page);
	SWIM_CHECK_EQUAL(red.Page, a.Page);
	SWIM_CHECK_EQUAL(a.Width, 5u);
	SWIM_CHECK_EQUAL(a.Height, 8u);
	SWIM_CHECK_NEAR(a.Top, 7.0f / 8.0f, 1e-6f); // Baseline 7 pixels below the cell's top.
	SWIM_CHECK_NEAR(a.Left, 0.0f, 1e-6f);
	SWIM_CHECK_EQUAL(atlas.GetPage(a.Page).Format, AtlasPageFormat::Color);
	SWIM_CHECK_EQUAL(atlas.GetPage(msdf.Page).Format, AtlasPageFormat::Distance);
	const auto page = atlas.GetPage(a.Page);
	SWIM_CHECK_EQUAL(page.Pixels.size(), std::size_t(64) * 64 * 4);
	const auto texel = [&](std::uint32_t x, std::uint32_t y)
	{
		return &page.Pixels[(std::size_t(y) * page.Size + x) * 4];
	};
	SWIM_CHECK_EQUAL(int(texel(a.X, a.Y)[3]), 0);		   // Row 0 of the cell is empty.
	SWIM_CHECK_EQUAL(int(texel(a.X + 4, a.Y + 1)[3]), 255); // Opaque body.
	SWIM_CHECK_EQUAL(int(texel(a.X - 1, a.Y + 1)[3]), 0);  // Gutter.
	SWIM_CHECK_EQUAL(int(texel(red.X, red.Y + 3)[0]), 255);
	SWIM_CHECK_EQUAL(int(texel(red.X, red.Y + 3)[1]), 0);
	SWIM_CHECK_EQUAL(atlas.Get(bitmap, U' ').Page, NoAtlasPage);
	SWIM_CHECK_EQUAL(atlas.Prewarm(bitmap, std::vector<std::uint32_t>{ U'A', U'i', U'l' }), 2u);
}

SWIM_TEST("Text.Bitmap", "FallsBackToVectorFacesPerCluster")
{
	const auto bitmap = MakeBitmapFontFixture();
	const auto vector = Swim::Testing::LoadTextFontFixture();
	const auto fonts = std::make_shared<const FontCollection>(std::vector<std::shared_ptr<const FontFace>>{ bitmap, vector });
	TextLayoutDesc desc;
	desc.Size = 16;
	const TextLayout layout("Ai\xC3\xA9 A", fonts, desc); // é is missing from the bitmap face.
	const auto& glyphs = layout.GetGlyphs();
	SWIM_REQUIRE(glyphs.size() >= 5);
	SWIM_CHECK_EQUAL(glyphs[0].Face, 0u);
	SWIM_CHECK_EQUAL(glyphs[1].Face, 0u);
	SWIM_CHECK_EQUAL(glyphs[2].Face, 1u);
	SWIM_CHECK_EQUAL(glyphs.back().Face, 0u);
	SWIM_CHECK_NEAR(glyphs[1].X, 12.0f, 1e-5f);
	SWIM_CHECK_NEAR(glyphs[2].X, 16.0f, 1e-5f);
	SWIM_CHECK_EQUAL(layout.GetMissingGlyphs(), 0u);
}

SWIM_TEST("Text.Bitmap", "BoldRangesWidenLayoutAndWrapping")
{
	const auto fonts = FontCollection::Single(MakeBitmapFontFixture());
	TextLayoutDesc desc;
	desc.Size = 8;
	const TextLayout regular("AA AA", fonts, desc);
	SWIM_CHECK_NEAR(regular.GetWidth(), 6 + 6 + 4 + 6 + 6, 1e-5f);
	desc.Styles = { { 3, true } };
	const TextLayout bold("AA AA", fonts, desc);
	SWIM_CHECK_NEAR(bold.GetWidth(), 6 + 6 + 4 + 7 + 7, 1e-5f);
	SWIM_CHECK_NEAR(bold.GetGlyphs()[4].X, 6 + 6 + 4 + 7, 1e-5f);
	// The bold word no longer fits a 28-unit line: wrapping uses the widened advances.
	desc.Wrap = TextWrap::Word;
	desc.MaxWidth = 28;
	SWIM_CHECK_EQUAL(TextLayout("AA AA", fonts, desc).GetLines().size(), 2u);
	desc.Styles.clear();
	SWIM_CHECK_EQUAL(TextLayout("AA AA", fonts, desc).GetLines().size(), 1u);
	desc.Styles = { { 3, true }, { 1, false } };
	SWIM_CHECK_THROWS(TextLayout("AA", fonts, desc), std::invalid_argument);
}

SWIM_TEST("Text.Bitmap", "ObfuscationKeepsTheAdvance")
{
	const auto face = MakeBitmapFontFixture();

	for (std::uint32_t seed = 0; seed < 16; ++seed)
	{
		const auto substitute = face->GetSubstituteGlyph(U'l', seed);
		SWIM_CHECK(substitute == U'l' || substitute == U'|'); // Both advance 3.
	}

	SWIM_CHECK_EQUAL(face->GetSubstituteGlyph(U'W', 7), std::uint32_t(U'W')); // Unique advance.
}
