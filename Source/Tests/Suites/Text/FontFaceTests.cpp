#include "Engine/Systems/Text/FontFace.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <stdexcept>

using namespace Swim::Text;
using Swim::Testing::LoadTextFontFixture;

SWIM_TEST("Text.Font", "OwnsBytesAndValidatesFontsAndSizes")
{
	const std::array<std::byte, 32> invalid{};
	SWIM_CHECK_THROWS(FontFace(invalid), std::invalid_argument);
	SWIM_CHECK_THROWS(FontFace(std::span<const std::byte>{}), std::invalid_argument);
	// The fixture's temporary byte vector was destroyed before this returns.
	const auto font = LoadTextFontFixture();
	SWIM_CHECK(font->GetGlyph(U'A') != 0);
	SWIM_CHECK_EQUAL(font->GetGlyph(U'\U0010FFFF'), 0u);
	const auto small = font->GetMetrics(20.0f);
	const auto large = font->GetMetrics(40.0f);
	SWIM_CHECK(small.Ascender > 0.0f);
	SWIM_CHECK(small.Descender < 0.0f);
	SWIM_CHECK(small.LineHeight > 0.0f);
	SWIM_CHECK_NEAR(large.LineHeight, small.LineHeight * 2.0f, 1e-5f);
	SWIM_CHECK_THROWS(font->Shape("text", 0), std::invalid_argument);
	SWIM_CHECK_THROWS(font->Shape("text", std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
	SWIM_CHECK_THROWS(font->GetMetrics(std::numeric_limits<float>::infinity()), std::invalid_argument);
	SWIM_CHECK_THROWS(font->Shape(std::string(1024 * 1024 + 1, 'x'), 20), std::invalid_argument);
}

SWIM_TEST("Text.Font", "ShapesKerningLigaturesAndCombiningClusters")
{
	const auto font = LoadTextFontFixture();
	ShapeOptions unkerned;
	unkerned.Kerning = false;
	SWIM_CHECK(font->Shape("AV", 32).AdvanceX < font->Shape("AV", 32, unkerned).AdvanceX);
	ShapeOptions unligated;
	unligated.Ligatures = false;
	const auto ligature = font->Shape("ffi", 32);
	const auto letters = font->Shape("ffi", 32, unligated);
	SWIM_CHECK_EQUAL(ligature.Glyphs.size(), 1u);
	SWIM_CHECK_EQUAL(letters.Glyphs.size(), 3u);
	SWIM_CHECK_EQUAL(ligature.Glyphs[0].Cluster, 0u);
	const auto accent = font->Shape("e\xCC\x81", 32);
	SWIM_REQUIRE(!accent.Glyphs.empty());
	for (const auto& glyph : accent.Glyphs)
	{
		SWIM_CHECK_EQUAL(glyph.Cluster, 0u);
	}
	SWIM_CHECK_EQUAL(accent.MissingGlyphs, 0u);
	const auto doubled = font->Shape("ffi", 64);
	SWIM_CHECK_NEAR(doubled.AdvanceX, ligature.AdvanceX * 2.0f, 1e-5f);
}

SWIM_TEST("Text.Font", "PreservesUtf8OffsetsAndShapesArabicRightToLeft")
{
	const auto font = LoadTextFontFixture();
	const auto utf8 = font->Shape("A\xF0\x9F\x98\x80"
								  "B",
		24);
	SWIM_REQUIRE_EQUAL(utf8.Glyphs.size(), 3u);
	SWIM_CHECK_EQUAL(utf8.Glyphs[0].Cluster, 0u);
	SWIM_CHECK_EQUAL(utf8.Glyphs[1].Cluster, 1u);
	SWIM_CHECK_EQUAL(utf8.Glyphs[2].Cluster, 5u);
	SWIM_CHECK_EQUAL(utf8.MissingGlyphs, 0u);
	const auto rtl = font->Shape("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", 24); // salaam
	SWIM_CHECK(rtl.Direction == TextDirection::RightToLeft);
	SWIM_CHECK_EQUAL(rtl.MissingGlyphs, 0u);
	SWIM_REQUIRE_EQUAL(rtl.Glyphs.size(), 3u); // Lam-alef ligature plus contextual seen and meem.
	SWIM_CHECK(rtl.Glyphs[0].Cluster > rtl.Glyphs.back().Cluster);
	SWIM_CHECK(rtl.AdvanceX > 0.0f);
	const auto empty = font->Shape({}, 24);
	SWIM_CHECK(empty.Glyphs.empty());
	SWIM_CHECK_EQUAL(empty.AdvanceX, 0.0f);
	const auto missing = font->Shape("\xF4\x8F\xBF\xBF", 24);
	SWIM_CHECK_EQUAL(missing.MissingGlyphs, 1u);
	const auto malformed = font->Shape("\xFF", 24);
	SWIM_REQUIRE_EQUAL(malformed.Glyphs.size(), 1u);
	SWIM_CHECK_EQUAL(malformed.Glyphs[0].Glyph, font->GetGlyph(U'\uFFFD'));
}

SWIM_TEST("Text.Font", "ImmutableShapingIsConsistentAcrossThreads")
{
	const auto font = LoadTextFontFixture();
	const auto expected = font->Shape("AV ffi e\xCC\x81", 28);
	std::vector<std::future<ShapedRun>> jobs;
	for (int i = 0; i < 8; ++i)
	{
		jobs.push_back(std::async(std::launch::async,
			[font]
			{
				return font->Shape("AV ffi e\xCC\x81", 28);
			}));
	}
	for (auto& job : jobs)
	{
		const auto run = job.get();
		SWIM_CHECK_EQUAL(run.AdvanceX, expected.AdvanceX);
		SWIM_REQUIRE_EQUAL(run.Glyphs.size(), expected.Glyphs.size());
		for (std::size_t i = 0; i < run.Glyphs.size(); ++i)
		{
			SWIM_CHECK_EQUAL(run.Glyphs[i].Glyph, expected.Glyphs[i].Glyph);
			SWIM_CHECK_EQUAL(run.Glyphs[i].Cluster, expected.Glyphs[i].Cluster);
		}
	}
}
