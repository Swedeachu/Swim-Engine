#include "Engine/Systems/Text/TextSegmentation.h"
#include "Engine/Systems/Text/Utf8.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>
#include <string>

using namespace Swim::Text;

namespace
{
	// Hebrew "shalom" (4 x 2 bytes) and Arabic "salam" (4 x 2 bytes).
	constexpr const char* Shalom = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
	constexpr const char* Salam = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";

	std::vector<std::uint32_t> Positions(const std::vector<std::uint8_t>& flags)
	{
		std::vector<std::uint32_t> result;
		for (std::uint32_t i = 0; i < flags.size(); ++i)
		{
			if (flags[i])
			{
				result.push_back(i);
			}
		}
		return result;
	}
} // namespace

SWIM_TEST("Text.Utf8", "DecodesSanitizesAndStepsBackwards")
{
	const std::string text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
	SWIM_CHECK(IsValidUtf8(text));
	SWIM_CHECK_EQUAL(DecodeUtf8(text, 1).CodePoint, U'é');
	SWIM_CHECK_EQUAL(DecodeUtf8(text, 3).CodePoint, U'€');
	SWIM_CHECK_EQUAL(DecodeUtf8(text, 6).CodePoint, U'\U0001F600');
	SWIM_CHECK_EQUAL(DecodeUtf8(text, 6).Length, 4u);
	SWIM_CHECK_EQUAL(PreviousUtf8(text, text.size()), 6u);
	SWIM_CHECK_EQUAL(PreviousUtf8(text, 6), 3u);
	SWIM_CHECK_EQUAL(PreviousUtf8(text, 3), 1u);
	// Truncated sequence, stray continuation, overlong form and a surrogate.
	for (const std::string& invalid :
		{ std::string("\xE2\x82"), std::string("\x80"), std::string("\xC0\xAF"), std::string("\xED\xA0\x80") })
	{
		SWIM_CHECK(!IsValidUtf8(invalid));
		const auto sanitized = SanitizeUtf8(invalid);
		SWIM_CHECK(IsValidUtf8(sanitized));
		SWIM_CHECK_EQUAL(sanitized.size(), invalid.size() * 3); // One U+FFFD per invalid byte.
	}
	SWIM_CHECK_EQUAL(SanitizeUtf8(text), text);
	std::string encoded;
	AppendUtf8(encoded, U'\U0010FFFF');
	AppendUtf8(encoded, 0xD800); // Surrogates encode as U+FFFD.
	SWIM_CHECK_EQUAL(encoded, std::string("\xF4\x8F\xBF\xBF\xEF\xBF\xBD"));
}

SWIM_TEST("Text.Segmentation", "FindsGraphemeWordAndLineBoundaries")
{
	// e + combining acute, CR LF, a ZWJ family emoji and a regional-indicator flag
	// are single grapheme clusters.
	const std::string text = "e\xCC\x81 \r\n\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9 \xF0\x9F\x87\xAB\xF0\x9F\x87\xB7";
	const auto boundaries = FindTextBoundaries(text);
	const auto graphemes = Positions(boundaries.Grapheme);
	const std::vector<std::uint32_t> expected{ 0, 3, 4, 6, 17, 18, 26 };
	SWIM_CHECK(graphemes == expected);
	SWIM_CHECK(boundaries.LineBreak[6] == LineBreakKind::Mandatory); // After CR LF.
	SWIM_CHECK(boundaries.LineBreak[5] == LineBreakKind::None);		 // Never between CR and LF.

	const std::string sentence = "hello, brave world";
	const auto words = FindTextBoundaries(sentence);
	SWIM_CHECK(words.LineBreak[7] == LineBreakKind::Allowed);  // Before "brave".
	SWIM_CHECK(words.LineBreak[13] == LineBreakKind::Allowed); // Before "world".
	SWIM_CHECK(words.LineBreak[6] == LineBreakKind::None);	   // Not between ',' and ' '.
	SWIM_CHECK(words.LineBreak[2] == LineBreakKind::None);
	SWIM_CHECK(words.LineBreak[sentence.size()] == LineBreakKind::Mandatory);
	const auto wordPositions = Positions(words.Word);
	const std::vector<std::uint32_t> wordExpected{ 0, 5, 6, 7, 12, 13, 18 };
	SWIM_CHECK(wordPositions == wordExpected);
	SWIM_CHECK(FindTextBoundaries("").Grapheme.size() == 1u);
	SWIM_CHECK_THROWS(FindTextBoundaries("\xFF"), std::invalid_argument);
}

SWIM_TEST("Text.Segmentation", "ItemizesScriptsWithCommonCharactersJoiningNeighbours")
{
	const std::string text = std::string("abc, ") + Shalom + " 123";
	const auto spans = FindScriptSpans(text);
	SWIM_REQUIRE_EQUAL(spans.size(), 2u);
	SWIM_CHECK_EQUAL(ScriptTagToString(spans[0].Tag), std::string("Latn"));
	SWIM_CHECK_EQUAL(spans[0].Begin, 0u);
	SWIM_CHECK_EQUAL(ScriptTagToString(spans[1].Tag), std::string("Hebr"));
	SWIM_CHECK_EQUAL(spans[1].End, static_cast<std::uint32_t>(text.size())); // Trailing common text joins Hebrew.
	SWIM_CHECK_EQUAL(spans[0].End, spans[1].Begin);
	SWIM_CHECK_EQUAL(MakeScriptTag("Arab"), 0x41726162u);
	SWIM_CHECK_EQUAL(MakeScriptTag("Ara"), 0u);
}

SWIM_TEST("Text.Segmentation", "ResolvesBidiParagraphsLevelsAndBaseDirections")
{
	const std::string mixed = std::string("abc ") + Shalom + " def";
	const auto ltr = AnalyzeBidi(mixed);
	SWIM_REQUIRE_EQUAL(ltr.size(), 1u);
	SWIM_CHECK_EQUAL(ltr[0].BaseLevel, 0u);
	SWIM_CHECK_EQUAL(ltr[0].Levels[0], 0u);
	SWIM_CHECK_EQUAL(ltr[0].Levels[4], 1u); // Hebrew.
	SWIM_CHECK_EQUAL(ltr[0].Levels[mixed.size() - 1], 0u);

	const std::string rtlFirst = std::string(Salam) + " abc";
	const auto rtl = AnalyzeBidi(rtlFirst);
	SWIM_CHECK_EQUAL(rtl[0].BaseLevel, 1u);
	SWIM_CHECK_EQUAL(rtl[0].Levels[0], 1u);
	SWIM_CHECK_EQUAL(rtl[0].Levels[rtlFirst.size() - 1], 2u); // Latin embedded in RTL.
	const auto forced = AnalyzeBidi(rtlFirst, TextDirection::LeftToRight);
	SWIM_CHECK_EQUAL(forced[0].BaseLevel, 0u);
	SWIM_CHECK_EQUAL(forced[0].Levels[0], 1u);
	const auto neutral = AnalyzeBidi("123", TextDirection::Auto);
	SWIM_CHECK_EQUAL(neutral[0].BaseLevel, 0u); // No strong character.

	// CR LF and LF separate paragraphs; a trailing separator adds an empty paragraph
	// that keeps the previous base direction.
	const std::string text = std::string("abc\r\n") + Salam + "\n";
	const auto paragraphs = AnalyzeBidi(text);
	SWIM_REQUIRE_EQUAL(paragraphs.size(), 3u);
	SWIM_CHECK_EQUAL(paragraphs[0].End, 3u);
	SWIM_CHECK_EQUAL(paragraphs[0].SeparatorLength, 2u);
	SWIM_CHECK_EQUAL(paragraphs[1].Begin, 5u);
	SWIM_CHECK_EQUAL(paragraphs[1].BaseLevel, 1u);
	SWIM_CHECK_EQUAL(paragraphs[2].Begin, static_cast<std::uint32_t>(text.size()));
	SWIM_CHECK_EQUAL(paragraphs[2].End, paragraphs[2].Begin);
	SWIM_CHECK_EQUAL(paragraphs[2].BaseLevel, 1u);
	SWIM_CHECK_EQUAL(AnalyzeBidi("").size(), 1u);
	SWIM_CHECK(IsHangingWhitespace(U' ') && IsHangingWhitespace(U'　') && !IsHangingWhitespace(U' '));
	SWIM_CHECK(IsLineSeparator(U' ') && !IsLineSeparator(U'\n'));
}
