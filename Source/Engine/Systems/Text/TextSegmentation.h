#pragma once

#include "Engine/Systems/Text/FontFace.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Text
{
	enum class LineBreakKind : std::uint8_t
	{
		None,	  // No break may occur before this byte.
		Allowed,  // A soft wrap opportunity (UAX #14).
		Mandatory // A hard break (after LF, CR LF, U+2028, U+2029, ...).
	};

	// Unicode boundaries of one valid UTF-8 text, indexed by byte offset. Every array
	// has text.size() + 1 entries; entry i describes the position before byte i.
	// Positions inside a code point are never boundaries.
	struct TextBoundaries
	{
		std::vector<std::uint8_t> Grapheme;	  // 1 at extended grapheme cluster boundaries (UAX #29), 0 and size included.
		std::vector<std::uint8_t> Word;		  // 1 at word boundaries (UAX #29), 0 and size included.
		std::vector<LineBreakKind> LineBreak; // Break opportunity before byte i (index 0 is None).
	};

	// libunibreak with the language tailoring (BCP 47, may be empty). Throws
	// std::invalid_argument for invalid UTF-8 (sanitize first) or text above 1 MiB.
	TextBoundaries FindTextBoundaries(std::string_view utf8, std::string_view language = {});

	// A maximal run of one Unicode script (UAX #24 via SheenBidi): Common and
	// Inherited characters join their neighbours. Tag is the ISO 15924 code as a
	// big-endian uint32 ('Latn' = 0x4C61746E); 0 when unknown.
	struct ScriptSpan
	{
		std::uint32_t Begin = 0;
		std::uint32_t End = 0;
		std::uint32_t Tag = 0;
	};

	std::vector<ScriptSpan> FindScriptSpans(std::string_view utf8);

	// One bidi paragraph (UAX #9 P1): [Begin, End) excludes its separator, which
	// occupies [End, End + SeparatorLength). Levels has one embedding level per
	// byte of [Begin, End) (resolved through rule I2, before line rules L1-L2).
	struct BidiParagraph
	{
		std::uint32_t Begin = 0;
		std::uint32_t End = 0;
		std::uint32_t SeparatorLength = 0;
		std::uint8_t BaseLevel = 0; // 0 left-to-right, 1 right-to-left.
		std::vector<std::uint8_t> Levels;
	};

	// Auto applies rules P2-P3 per paragraph (first strong character, else LTR).
	// Text ending with a separator yields a final empty paragraph.
	std::vector<BidiParagraph> AnalyzeBidi(std::string_view utf8, TextDirection direction = TextDirection::Auto);

	// Spaces that hang past the line end and do not count toward wrapping (space,
	// tab, ideographic and typographic spaces; not the no-break spaces).
	bool IsHangingWhitespace(char32_t codePoint);
	// Characters that end a line without ending the bidi paragraph (U+000B, U+000C, U+2028).
	bool IsLineSeparator(char32_t codePoint);

	// ISO 15924 tag helpers ("Arab" <-> 0x41726162).
	std::uint32_t MakeScriptTag(std::string_view code);
	std::string ScriptTagToString(std::uint32_t tag);
} // namespace Swim::Text
