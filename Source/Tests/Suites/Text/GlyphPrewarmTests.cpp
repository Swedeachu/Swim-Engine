#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/UI/UiDocument.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Swim::Text;

namespace
{
	// A minimal parallel-for over four threads, standing in for JobSystem::ParallelFor.
	void ThreadedFor(std::size_t count, const std::function<void(std::size_t)>& body)
	{
		std::atomic<std::size_t> next{ 0 };
		std::vector<std::thread> workers;
		for (int t = 0; t < 4; ++t)
		{
			workers.emplace_back(
				[&]
				{
					for (std::size_t i = next++; i < count; i = next++)
					{
						body(i);
					}
				});
		}
		for (auto& worker : workers)
		{
			worker.join();
		}
	}
} // namespace

SWIM_TEST("Text.Atlas", "ParallelPrewarmPacksExactlyLikeSequentialLookups")
{
	const auto font = Swim::Testing::LoadTextFontFixture();
	std::vector<std::uint32_t> glyphs;
	for (const char32_t c : std::u32string(U"The quick brown fox, 0123456789 AVffi @#%"))
	{
		glyphs.push_back(font->GetGlyph(c));
	}
	GlyphAtlas sequential;
	for (const auto glyph : glyphs)
	{
		sequential.Get(font, glyph);
	}
	GlyphAtlas prewarmed;
	const auto added = prewarmed.Prewarm(font, glyphs, ThreadedFor);
	SWIM_CHECK_EQUAL(added, sequential.GetGlyphCount());
	SWIM_CHECK_EQUAL(prewarmed.GetGlyphCount(), sequential.GetGlyphCount());
	SWIM_REQUIRE_EQUAL(prewarmed.GetPageCount(), sequential.GetPageCount());
	for (const auto glyph : glyphs)
	{
		const auto a = sequential.Get(font, glyph);
		const auto b = prewarmed.Get(font, glyph);
		SWIM_CHECK(a.Page == b.Page && a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height);
	}
	const auto pageA = sequential.GetPage(0);
	const auto pageB = prewarmed.GetPage(0);
	SWIM_CHECK(std::equal(pageA.Pixels.begin(), pageA.Pixels.end(), pageB.Pixels.begin()));
	SWIM_CHECK_EQUAL(pageA.Revision, pageB.Revision);
	SWIM_CHECK_EQUAL(prewarmed.Prewarm(font, glyphs, ThreadedFor), 0u); // Everything cached.
	SWIM_CHECK(prewarmed.Contains(*font, glyphs[0]));

	// A failing glyph inserts nothing.
	GlyphAtlas failing;
	const std::uint32_t invalid[] = { font->GetGlyph(U'a'), 1000000u };
	SWIM_CHECK_THROWS(failing.Prewarm(font, invalid, ThreadedFor), std::out_of_range);
	SWIM_CHECK_EQUAL(failing.GetGlyphCount(), 0u);
	SWIM_CHECK_THROWS(failing.Prewarm(nullptr, invalid), std::invalid_argument);
}

SWIM_TEST("UI.Text", "PrewarmGlyphsFillsTheAtlasBeforePaint")
{
	using namespace Swim::UI;
	const auto chain = Swim::Testing::LoadTextFontChain();
	UiDocument ui;
	const auto a = ui.Create(ui.GetRoot());
	ui.SetText(a, chain, "Prewarm me \xCE\xA9\xCE\xB1", 20);
	const auto b = ui.Create(ui.GetRoot());
	ui.SetText(b, chain, "and me too", 20);
	GlyphAtlas atlas;
	SWIM_CHECK_THROWS(ui.PrewarmGlyphs(atlas), std::logic_error); // Needs a layout.
	ui.Layout({ 400, 200 });
	const auto added = ui.PrewarmGlyphs(atlas, ThreadedFor);
	SWIM_CHECK(added > 10u);
	const auto count = atlas.GetGlyphCount();
	const auto revision = atlas.GetPage(0).Revision;
	ui.Paint(atlas);
	SWIM_CHECK_EQUAL(atlas.GetGlyphCount(), count); // Paint only looked glyphs up.
	SWIM_CHECK_EQUAL(atlas.GetPage(0).Revision, revision);
	SWIM_CHECK_EQUAL(ui.PrewarmGlyphs(atlas), 0u);
}

// The fixture's 'u' carries a point-only contour above its bowl (as fonts from subsetting
// or hinting tools may). It must not reach msdfgen: an empty contour in the overlap-aware
// combiner used to draw a streak through the open top of the glyph.
SWIM_TEST("Text.Atlas", "PointOnlyContoursLeaveNoDistanceFieldArtifacts")
{
	const auto font = Swim::Testing::LoadTextFontFixture();
	Swim::Text::GlyphAtlas atlas;
	const auto entry = atlas.Get(font, font->GetGlyph(U'u'));
	SWIM_REQUIRE(entry.Page != Swim::Text::NoAtlasPage);
	const auto page = atlas.GetPage(entry.Page);
	// Between the stems, above the bowl, every texel is outside the glyph (median < 0.5).
	std::uint32_t inside = 0;
	for (std::uint32_t y = 0; y < 20; ++y)
	{
		for (std::uint32_t x = 9; x <= 19; ++x)
		{
			const auto* texel = &page.Pixels[(std::size_t(entry.Y + y) * page.Size + entry.X + x) * 3];
			const auto r = texel[0], g = texel[1], b = texel[2];
			const auto median = std::max(std::min(r, g), std::min(std::max(r, g), b));
			inside += median >= 128 ? 1u : 0u;
		}
	}
	SWIM_CHECK_EQUAL(inside, 0u);
	SWIM_CHECK(entry.Width >= 26u && entry.Height >= 30u);
}
