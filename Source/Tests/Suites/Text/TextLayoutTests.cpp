#include "Engine/Systems/Text/TextLayout.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

using namespace Swim::Text;

namespace
{
	constexpr const char* Shalom = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";		  // Hebrew, 8 bytes.
	constexpr const char* Omega = "\xCE\xA9\xCE\xBC\xCE\xAD\xCE\xB3\xCE\xB1"; // Greek, 10 bytes.
	constexpr const char* Salam = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";		  // Arabic, 8 bytes.

	std::shared_ptr<const FontCollection> Primary()
	{
		return FontCollection::Single(Swim::Testing::LoadTextFontFixture());
	}

	TextLayoutDesc Desc(float size = 20.0f)
	{
		TextLayoutDesc desc;
		desc.Size = size;
		return desc;
	}

	float LineVisibleRight(const TextLine& line)
	{
		return line.X + line.Width;
	}
} // namespace

SWIM_TEST("Text.Layout", "SingleLineMatchesShapingAndKeepsGlyphOrigins")
{
	const auto fonts = Primary();
	const TextLayout layout("AVffi", fonts, Desc(24));
	const auto shaped = fonts->GetPrimary().Shape("AVffi", 24);
	SWIM_REQUIRE_EQUAL(layout.GetLines().size(), 1u);
	SWIM_CHECK_NEAR(layout.GetWidth(), shaped.AdvanceX, 1e-4f);
	SWIM_REQUIRE_EQUAL(layout.GetGlyphs().size(), shaped.Glyphs.size());
	float pen = 0.0f;
	for (std::size_t i = 0; i < shaped.Glyphs.size(); ++i)
	{
		SWIM_CHECK_EQUAL(layout.GetGlyphs()[i].Glyph, shaped.Glyphs[i].Glyph);
		SWIM_CHECK_NEAR(layout.GetGlyphs()[i].X, pen + shaped.Glyphs[i].OffsetX, 1e-4f);
		SWIM_CHECK_NEAR(layout.GetGlyphs()[i].Y, layout.GetLines()[0].Baseline, 1e-4f);
		pen += shaped.Glyphs[i].AdvanceX;
	}
	const auto metrics = fonts->GetPrimary().GetMetrics(24);
	SWIM_CHECK_NEAR(layout.GetLines()[0].Baseline, metrics.Ascender, 1e-4f);
	SWIM_CHECK_NEAR(layout.GetHeight(), metrics.LineHeight, 1e-4f);
	SWIM_CHECK_EQUAL(layout.GetMissingGlyphs(), 0u);
}

SWIM_TEST("Text.Layout", "WrapsAtOpportunitiesHangsSpacesAndBreaksLongWords")
{
	const auto fonts = Primary();
	const auto& face = fonts->GetPrimary();
	auto desc = Desc(20);
	desc.Wrap = TextWrap::Word;
	desc.MaxWidth = face.Shape("alpha beta", 20).AdvanceX + 0.5f; // Two words fit, three do not.
	const TextLayout layout("alpha beta gamma", fonts, desc);
	SWIM_REQUIRE_EQUAL(layout.GetLines().size(), 2u);
	const auto& first = layout.GetLines()[0];
	SWIM_CHECK_EQUAL(first.Begin, 0u);
	SWIM_CHECK_EQUAL(first.End, 11u); // "alpha beta " keeps its hanging space.
	SWIM_CHECK(!first.HardBreak);
	SWIM_CHECK_NEAR(first.Width, face.Shape("alpha beta", 20).AdvanceX, 1e-3f);
	SWIM_CHECK_EQUAL(layout.GetLines()[1].Begin, 11u);
	SWIM_CHECK(layout.GetLines()[1].HardBreak);
	for (const auto& line : layout.GetLines())
	{
		SWIM_CHECK(line.Width <= desc.MaxWidth);
	}
	SWIM_CHECK_NEAR(layout.GetLines()[1].Top, first.Height, 1e-4f);

	// A word wider than the line breaks between graphemes, at least one per line.
	desc.MaxWidth = face.Shape("mmm", 20).AdvanceX + 0.25f;
	const TextLayout narrow("mmmmmmmm", fonts, desc);
	SWIM_REQUIRE_EQUAL(narrow.GetLines().size(), 3u);
	SWIM_CHECK_EQUAL(narrow.GetLines()[0].End, 3u);
	SWIM_CHECK_EQUAL(narrow.GetLines()[2].End, 8u);
	desc.MaxWidth = 1.0f;
	const TextLayout tiny("ab", fonts, desc);
	SWIM_CHECK_EQUAL(tiny.GetLines().size(), 2u); // Every grapheme gets a line even when too wide.

	// Without wrapping only hard breaks split lines, including U+2028 and CR LF.
	const TextLayout hard("one\xE2\x80\xA8two\r\nthree\n", fonts, Desc(20));
	SWIM_REQUIRE_EQUAL(hard.GetLines().size(), 4u);
	SWIM_CHECK_EQUAL(hard.GetLines()[0].End, 3u);
	SWIM_CHECK_EQUAL(hard.GetLines()[1].Begin, 6u);
	SWIM_CHECK_EQUAL(hard.GetLines()[1].End, 9u);
	SWIM_CHECK_EQUAL(hard.GetLines()[2].Begin, 11u);
	SWIM_CHECK_EQUAL(hard.GetLines()[3].Begin, hard.GetLines()[3].End); // Empty final line.
	SWIM_CHECK_EQUAL(hard.GetMissingGlyphs(), 0u);						// U+2028 is never shaped.
	SWIM_CHECK_EQUAL(TextLayout("", fonts, Desc(20)).GetLines().size(), 1u);
}

SWIM_TEST("Text.Layout", "AlignsLinesAndResolvesStartAndEndByParagraphDirection")
{
	const auto fonts = Primary();
	auto desc = Desc(20);
	desc.MaxWidth = 300.0f;
	const auto widthOf = [&](const char* text)
	{
		return fonts->GetPrimary().Shape(text, 20).AdvanceX;
	};
	desc.Align = TextAlign::Center;
	const TextLayout centered("abc", fonts, desc);
	SWIM_CHECK_NEAR(centered.GetLines()[0].X, (300.0f - widthOf("abc")) * 0.5f, 1e-3f);
	desc.Align = TextAlign::End;
	const TextLayout end("abc", fonts, desc);
	SWIM_CHECK_NEAR(LineVisibleRight(end.GetLines()[0]), 300.0f, 1e-3f);
	desc.Align = TextAlign::Start;
	const TextLayout arabic(Salam, fonts, desc);
	SWIM_CHECK_EQUAL(arabic.GetLines()[0].BaseLevel, 1u);
	SWIM_CHECK_NEAR(LineVisibleRight(arabic.GetLines()[0]), 300.0f, 1e-3f); // Start is the right edge in RTL.
	desc.Direction = TextDirection::LeftToRight;
	const TextLayout forced(Salam, fonts, desc);
	SWIM_CHECK_NEAR(forced.GetLines()[0].X, 0.0f, 1e-4f);
	// Infinite width aligns against the widest line.
	auto open = Desc(20);
	open.Align = TextAlign::Right;
	const TextLayout lines("a\nabc", fonts, open);
	SWIM_CHECK_NEAR(LineVisibleRight(lines.GetLines()[0]), widthOf("abc"), 1e-3f);
	SWIM_CHECK_NEAR(lines.GetWidth(), widthOf("abc"), 1e-3f);
	// A trailing space hangs past the right edge instead of pushing text left.
	desc = Desc(20);
	desc.MaxWidth = 300.0f;
	desc.Align = TextAlign::Right;
	const TextLayout spaced("abc   ", fonts, desc);
	SWIM_CHECK_NEAR(LineVisibleRight(spaced.GetLines()[0]), 300.0f, 1e-3f);
	auto spacing = Desc(20);
	spacing.LineSpacing = 1.5f;
	const TextLayout loose("a\nb", fonts, spacing);
	SWIM_CHECK_NEAR(loose.GetHeight(), 2.0f * 1.5f * fonts->GetPrimary().GetMetrics(20).LineHeight, 1e-3f);
}

SWIM_TEST("Text.Layout", "ReordersMixedDirectionTextPerLine")
{
	const auto fonts = Primary();
	const std::string text = std::string("abc ") + Salam + " def";
	const TextLayout layout(text, fonts, Desc(20));
	const auto& line = layout.GetLines()[0];
	SWIM_CHECK_EQUAL(line.BaseLevel, 0u);
	SWIM_REQUIRE(line.RunCount >= 3u);
	// Visual order: Latin, Arabic (level 1), Latin; runs are contiguous left to right.
	const auto* runs = &layout.GetRuns()[line.FirstRun];
	SWIM_CHECK_EQUAL(runs[0].Begin, 0u);
	bool sawArabic = false;
	for (std::uint32_t r = 0; r < line.RunCount; ++r)
	{
		if (r > 0)
		{
			SWIM_CHECK_NEAR(runs[r].X, runs[r - 1].X + runs[r - 1].Width, 1e-3f);
		}
		if ((runs[r].Level & 1) != 0)
		{
			sawArabic = true;
			SWIM_CHECK_EQUAL(ScriptTagToString(runs[r].Script), std::string("Arab"));
			// Right-to-left: later clusters are further left.
			const auto* glyphs = &layout.GetGlyphs()[runs[r].FirstGlyph];
			for (std::uint32_t g = 1; g < runs[r].GlyphCount; ++g)
			{
				SWIM_CHECK(glyphs[g].Cluster <= glyphs[g - 1].Cluster);
			}
		}
	}
	SWIM_CHECK(sawArabic);
	SWIM_CHECK_EQUAL(runs[line.RunCount - 1].End, static_cast<std::uint32_t>(text.size()));

	// In an RTL paragraph the Latin word sits left of the Arabic one.
	const std::string rtl = std::string(Salam) + " abc";
	const TextLayout reversed(rtl, fonts, Desc(20));
	const auto& rtlLine = reversed.GetLines()[0];
	SWIM_CHECK_EQUAL(rtlLine.BaseLevel, 1u);
	const auto& leftRun = reversed.GetRuns()[rtlLine.FirstRun];
	SWIM_CHECK_EQUAL(ScriptTagToString(leftRun.Script), std::string("Latn"));
	SWIM_CHECK(reversed.GetCaret(0).X > reversed.GetCaret(static_cast<std::uint32_t>(rtl.size())).X);
}

SWIM_TEST("Text.Layout", "FallsBackPerGraphemeClusterAndKeepsNeutralsInRuns")
{
	const auto chain = Swim::Testing::LoadTextFontChain();
	const std::string text = std::string("Hi ") + Omega + ", " + Shalom + "!";
	const TextLayout layout(text, chain, Desc(20));
	SWIM_CHECK_EQUAL(layout.GetMissingGlyphs(), 0u);
	bool greek = false;
	bool hebrew = false;
	for (const auto& run : layout.GetRuns())
	{
		const std::string script = ScriptTagToString(run.Script);
		if (script == "Grek" && run.Face == 1)
		{
			greek = true;
		}
		if (script == "Hebr" && run.Face == 1 && (run.Level & 1) != 0)
		{
			hebrew = true;
		}
		if (script == "Latn")
		{
			SWIM_CHECK_EQUAL(run.Face, 0u);
		}
	}
	SWIM_CHECK(greek);
	SWIM_CHECK(hebrew);
	for (const auto& glyph : layout.GetGlyphs())
	{
		SWIM_CHECK(glyph.Glyph != 0);
	}
	// The primary face alone renders .notdef for the uncovered clusters.
	const TextLayout primaryOnly(text, Primary(), Desc(20));
	SWIM_CHECK_EQUAL(primaryOnly.GetMissingGlyphs(), 9u); // 5 Greek + 4 Hebrew letters.

	// Selection prefers the previous face for neutral characters it covers, and
	// ignores joiners and variation selectors.
	const char32_t alpha[] = { U'α' };
	const char32_t comma[] = { U',' };
	const char32_t joined[] = { U'א', U'‍' };
	SWIM_CHECK_EQUAL(chain->SelectFace(alpha), 1u);
	SWIM_CHECK_EQUAL(chain->SelectFace(comma, 1), 0u); // The fallback has no comma.
	SWIM_CHECK_EQUAL(chain->SelectFace(joined), 1u);
	SWIM_CHECK_THROWS(FontCollection({}), std::invalid_argument);
	SWIM_CHECK_THROWS(FontCollection({ nullptr }), std::invalid_argument);
}

SWIM_TEST("Text.Layout", "CaretsHitTestingAndSelectionRoundTrip")
{
	const auto fonts = Primary();
	const std::string text = "hello e\xCC\x81t\xC3\xA9";
	const TextLayout layout(text, fonts, Desc(20));
	const float y = layout.GetLines()[0].Top + 5.0f;
	std::uint32_t stops = 0;
	float previousX = -1.0f;
	for (std::uint32_t offset = 0; offset <= text.size(); ++offset)
	{
		if (!layout.IsCaretStop(offset))
		{
			continue;
		}
		++stops;
		const auto caret = layout.GetCaret(offset);
		SWIM_CHECK(caret.X > previousX);
		previousX = caret.X;
		SWIM_CHECK_EQUAL(layout.HitTest(caret.X + 0.1f, y), offset);
	}
	SWIM_CHECK_EQUAL(stops, 10u);		// Nine graphemes: "hello " 6, e + acute, t, e-acute; plus the end.
	SWIM_CHECK(!layout.IsCaretStop(7)); // Inside e + combining acute.
	SWIM_CHECK_EQUAL(layout.NextCaretStop(6), 9u);
	SWIM_CHECK_EQUAL(layout.PreviousCaretStop(9), 6u);
	SWIM_CHECK_EQUAL(layout.HitTest(-100.0f, y), 0u);
	SWIM_CHECK_EQUAL(layout.HitTest(1.0e6f, y), static_cast<std::uint32_t>(text.size()));
	const auto rects = layout.GetSelectionRects(1, 4);
	SWIM_REQUIRE_EQUAL(rects.size(), 1u);
	SWIM_CHECK_NEAR(rects[0].X, layout.GetCaret(1).X, 1e-3f);
	SWIM_CHECK_NEAR(rects[0].X + rects[0].Width, layout.GetCaret(4).X, 1e-3f);
	SWIM_CHECK(layout.GetSelectionRects(3, 3).empty());

	// Right-to-left carets move leftwards; a mixed selection is two visual pieces.
	const TextLayout arabic(Salam, fonts, Desc(20));
	SWIM_CHECK(arabic.GetCaret(0).X > arabic.GetCaret(2).X);
	SWIM_CHECK(arabic.GetCaret(0).RightToLeft);
	const std::string mixed = std::string("ab ") + Salam + " cd";
	const TextLayout both(mixed, fonts, Desc(20));
	SWIM_CHECK_EQUAL(both.GetSelectionRects(1, 7).size(), 2u);
	// Ligature carets split the ligature's advance: "ffi" has four stops.
	const TextLayout ligature("ffi", fonts, Desc(20));
	SWIM_REQUIRE_EQUAL(ligature.GetGlyphs().size(), 1u);
	SWIM_CHECK_NEAR(ligature.GetCaret(1).X, ligature.GetGlyphs()[0].Advance / 3.0f, 1e-3f);
}

SWIM_TEST("Text.Layout", "MovesByWordsAndLines")
{
	const auto fonts = Primary();
	const std::string text = "hello brave  world";
	const TextLayout layout(text, fonts, Desc(20));
	SWIM_CHECK_EQUAL(layout.NextWord(0), 6u);
	SWIM_CHECK_EQUAL(layout.NextWord(6), 13u);
	SWIM_CHECK_EQUAL(layout.NextWord(13), 18u);
	SWIM_CHECK_EQUAL(layout.NextWord(18), 18u);
	SWIM_CHECK_EQUAL(layout.PreviousWord(18), 13u);
	SWIM_CHECK_EQUAL(layout.PreviousWord(13), 6u);
	SWIM_CHECK_EQUAL(layout.PreviousWord(8), 6u);
	SWIM_CHECK_EQUAL(layout.PreviousWord(6), 0u);

	auto desc = Desc(20);
	desc.Wrap = TextWrap::Word;
	desc.MaxWidth = fonts->GetPrimary().Shape("hello brave", 20).AdvanceX + 0.5f;
	const TextLayout wrapped("hello brave world ok", fonts, desc);
	SWIM_REQUIRE_EQUAL(wrapped.GetLines().size(), 2u);
	SWIM_CHECK_EQUAL(wrapped.LineStart(3), 0u);
	SWIM_CHECK_EQUAL(wrapped.LineEnd(3), 11u);		// Before the hanging space of the soft line.
	SWIM_CHECK_EQUAL(wrapped.GetLineIndex(12), 1u); // A soft break belongs to the next line.
	SWIM_CHECK_EQUAL(wrapped.LineEnd(14), 20u);
	const float x = wrapped.GetCaret(3).X;
	const auto below = wrapped.LineBelow(3, x);
	SWIM_CHECK_EQUAL(wrapped.GetLineIndex(below), 1u);
	SWIM_CHECK(std::abs(wrapped.GetCaret(below).X - x) < fonts->GetPrimary().Shape("w", 20).AdvanceX);
	SWIM_CHECK_EQUAL(wrapped.LineAbove(below, x), 3u);
	SWIM_CHECK_EQUAL(wrapped.LineAbove(3, x), 3u);
}

SWIM_TEST("Text.Layout", "SanitizesInputAndValidatesDescriptions")
{
	const auto fonts = Primary();
	const TextLayout invalid("a\xFF"
							 "b",
		fonts, Desc(20));
	SWIM_CHECK_EQUAL(invalid.GetText(),
		std::string("a\xEF\xBF\xBD"
					"b"));
	SWIM_CHECK_EQUAL(invalid.GetMissingGlyphs(), 0u); // The fixture has U+FFFD.
	auto desc = Desc(20);
	desc.MaxWidth = 0.0f;
	SWIM_CHECK_THROWS(TextLayout("a", fonts, desc), std::invalid_argument);
	desc = Desc(std::numeric_limits<float>::quiet_NaN());
	SWIM_CHECK_THROWS(TextLayout("a", fonts, desc), std::invalid_argument);
	desc = Desc(20);
	desc.LineSpacing = 0.0f;
	SWIM_CHECK_THROWS(TextLayout("a", fonts, desc), std::invalid_argument);
	SWIM_CHECK_THROWS(TextLayout("a", nullptr, Desc(20)), std::invalid_argument);
	SWIM_CHECK_THROWS(TextLayout(std::string(1024 * 1024 + 1, 'a'), fonts, Desc(20)), std::length_error);
	const TextLayout empty;
	SWIM_CHECK(empty.GetLines().empty());
	SWIM_CHECK_EQUAL(empty.HitTest(0, 0), 0u);
}
