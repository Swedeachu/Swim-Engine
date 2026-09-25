#pragma once

#include "Engine/Systems/Text/FontCollection.h"
#include "Engine/Systems/Text/TextSegmentation.h"

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Text
{
	enum class TextAlign : std::uint8_t
	{
		Start, // Left in left-to-right paragraphs, right in right-to-left ones.
		End,
		Left,
		Right,
		Center
	};

	enum class TextWrap : std::uint8_t
	{
		None, // Hard breaks only.
		Word  // UAX #14 opportunities; a word wider than the line breaks between graphemes.
	};

	struct TextLayoutDesc
	{
		float Size = 16.0f; // Logical units per em, (0, 16384].
		// Base direction of every paragraph. Auto applies UAX #9 rules P2-P3 (the first
		// strong character decides; left-to-right without one).
		TextDirection Direction = TextDirection::Auto;
		TextAlign Align = TextAlign::Start;
		TextWrap Wrap = TextWrap::None;
		// Wrapping width, and the alignment box when finite. Infinite aligns lines
		// against the widest line. Must be > 0.
		float MaxWidth = std::numeric_limits<float>::infinity();
		float LineSpacing = 1.0f; // Multiplies the tallest face's line height, [0.25, 8].
		std::string Language;	  // BCP 47: shaping language and line-break tailoring.
		bool Kerning = true;
		bool Ligatures = true;
	};

	// One positioned glyph. X/Y are the glyph origin in layout space: top-left origin,
	// positive Y down, Y on the baseline (shaping offsets applied).
	struct TextGlyph
	{
		std::uint32_t Glyph = 0;
		std::uint32_t Face = 0;	   // Index into the FontCollection.
		std::uint32_t Cluster = 0; // Byte offset of the glyph's cluster in GetText().
		float X = 0.0f;
		float Y = 0.0f;
		float Advance = 0.0f;
	};

	// A shaped run of one face, script and bidi level, in visual order within its line.
	struct TextRun
	{
		std::uint32_t Begin = 0; // Logical byte range.
		std::uint32_t End = 0;
		std::uint32_t Face = 0;
		std::uint32_t Script = 0; // ISO 15924 tag (see MakeScriptTag).
		std::uint8_t Level = 0;	  // Odd = right-to-left.
		float X = 0.0f;			  // Left edge of the run's pen range.
		float Width = 0.0f;
		std::uint32_t FirstGlyph = 0;
		std::uint32_t GlyphCount = 0;
	};

	struct TextLine
	{
		std::uint32_t Begin = 0; // Logical content range, excluding hard-break characters.
		std::uint32_t End = 0;
		std::uint32_t Next = 0; // Where the following line begins (after a separator or soft break).
		float Top = 0.0f;
		float Baseline = 0.0f;
		float Height = 0.0f;
		float Ascent = 0.0f;
		float Descent = 0.0f; // Positive below the baseline.
		float X = 0.0f;		  // Left edge of the visible content (trailing whitespace hangs).
		float Width = 0.0f;	  // Visible width, excluding trailing whitespace.
		std::uint32_t FirstRun = 0;
		std::uint32_t RunCount = 0;
		std::uint8_t BaseLevel = 0; // Paragraph level.
		bool HardBreak = true;		// False when the line ends at a soft wrap.
	};

	struct TextRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
	};

	struct TextCaret
	{
		float X = 0.0f;
		float Top = 0.0f;
		float Height = 0.0f;
		std::uint32_t Line = 0;
		bool RightToLeft = false; // Direction of the character the caret is attached to.
	};

	// Paragraph layout (critical-path item 79): UTF-8 -> bidi paragraphs (UAX #9) ->
	// script (UAX #24) and font-fallback items per grapheme cluster -> shaping ->
	// line breaking (UAX #14, emergency grapheme breaks) -> per-line bidi reordering
	// (L1-L2) -> alignment. Also answers caret, hit-test, selection and cursor
	// movement queries in byte offsets of GetText().
	//
	// Immutable after construction; const queries are thread-safe. Invalid UTF-8 is
	// replaced (SanitizeUtf8), so offsets refer to GetText(), not the input. Input is
	// bounded to 1 MiB. Lines are shaped once for measurement and once per line
	// (edge contexts are not shared across a soft break).
	class TextLayout final
	{
	  public:
		TextLayout() = default;
		// Throws std::invalid_argument for a null collection or invalid desc,
		// std::length_error above 1 MiB.
		TextLayout(std::string_view utf8, std::shared_ptr<const FontCollection> fonts, const TextLayoutDesc& desc);

		const std::string& GetText() const { return text; }

		const TextLayoutDesc& GetDesc() const { return desc; }

		const std::shared_ptr<const FontCollection>& GetFonts() const { return fonts; }

		const std::vector<TextLine>& GetLines() const { return lines; }

		const std::vector<TextRun>& GetRuns() const { return runs; }

		const std::vector<TextGlyph>& GetGlyphs() const { return glyphs; }

		float GetWidth() const { return width; } // Widest visible line.

		float GetHeight() const { return height; } // Sum of line heights.

		std::uint32_t GetMissingGlyphs() const { return missingGlyphs; }

		// --- Caret and editing queries (offsets are grapheme boundaries of GetText()). ---
		bool IsCaretStop(std::uint32_t offset) const;
		std::uint32_t GetLineIndex(std::uint32_t offset) const; // Downstream affinity at soft breaks.
		TextCaret GetCaret(std::uint32_t offset) const;
		// The nearest caret stop to a point in layout space.
		std::uint32_t HitTest(float x, float y) const;
		// One rectangle per contiguous visual piece of [begin, end), line height tall.
		std::vector<TextRect> GetSelectionRects(std::uint32_t begin, std::uint32_t end) const;
		std::uint32_t NextCaretStop(std::uint32_t offset) const;
		std::uint32_t PreviousCaretStop(std::uint32_t offset) const;
		std::uint32_t NextWord(std::uint32_t offset) const;		// Start of the next word.
		std::uint32_t PreviousWord(std::uint32_t offset) const; // Start of this or the previous word.
		std::uint32_t LineStart(std::uint32_t offset) const;
		std::uint32_t LineEnd(std::uint32_t offset) const;
		// Vertical movement keeping a preferred x; returns the offset unchanged on the first/last line.
		std::uint32_t LineAbove(std::uint32_t offset, float x) const;
		std::uint32_t LineBelow(std::uint32_t offset, float x) const;

	  private:
		// Visual extent of one grapheme cluster in its line.
		struct Grapheme
		{
			std::uint32_t Begin = 0;
			std::uint32_t End = 0;
			std::uint32_t Line = 0;
			float Left = 0.0f;
			float Right = 0.0f;
			bool RightToLeft = false;
		};

		void Build();
		const Grapheme* FindGrapheme(std::uint32_t offset) const; // The grapheme starting at offset.
		float CaretX(std::uint32_t line, std::uint32_t offset) const;
		std::uint32_t HitTestLine(std::uint32_t line, float x) const;
		std::uint32_t ClampOffset(std::uint32_t offset) const;

		std::string text;
		std::shared_ptr<const FontCollection> fonts;
		TextLayoutDesc desc;
		TextBoundaries boundaries;
		std::vector<TextLine> lines;
		std::vector<TextRun> runs;
		std::vector<TextGlyph> glyphs;
		std::vector<Grapheme> graphemes; // Logical order.
		float width = 0.0f;
		float height = 0.0f;
		std::uint32_t missingGlyphs = 0;

		friend struct TextLayoutBuilder;
	};
} // namespace Swim::Text
