#include "Engine/Systems/Text/TextSegmentation.h"
#include "Engine/Systems/Text/Utf8.h"

#include <SheenBidi/SheenBidi.h>
#include <graphemebreak.h>
#include <linebreak.h>
#include <wordbreak.h>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace Swim::Text
{
	namespace
	{
		constexpr std::size_t MaxSegmentedBytes = 1024u * 1024u;

		void InitializeUnibreak()
		{
			static std::once_flag once;
			std::call_once(once,
				[]
				{
					init_linebreak();
					init_graphemebreak();
					init_wordbreak();
				});
		}

		void RequireSegmentable(std::string_view utf8)
		{
			if (utf8.size() > MaxSegmentedBytes)
			{
				throw std::length_error("Text segmentation input exceeds 1 MiB");
			}
			if (!IsValidUtf8(utf8))
			{
				throw std::invalid_argument("Text segmentation requires valid UTF-8 (use SanitizeUtf8)");
			}
		}

		// libunibreak reports the break *after* the code unit at i; the last code unit
		// of every code point carries the decision. Returns the break before byte i + 1.
		template <typename Fill> std::vector<char> Breaks(std::string_view utf8, std::string_view language, Fill fill)
		{
			std::vector<char> breaks(utf8.size());
			if (!utf8.empty())
			{
				const std::string lang(language);
				fill(reinterpret_cast<const utf8_t*>(utf8.data()), utf8.size(), lang.empty() ? nullptr : lang.c_str(), breaks.data());
			}
			return breaks;
		}

		struct AlgorithmDeleter
		{
			void operator()(const _SBAlgorithm* algorithm) const { SBAlgorithmRelease(algorithm); }
		};

		struct ParagraphDeleter
		{
			void operator()(const _SBParagraph* paragraph) const { SBParagraphRelease(paragraph); }
		};
	} // namespace

	TextBoundaries FindTextBoundaries(std::string_view utf8, std::string_view language)
	{
		RequireSegmentable(utf8);
		if (language.size() > 128)
		{
			throw std::invalid_argument("Text language tag exceeds 128 bytes");
		}
		InitializeUnibreak();
		const auto n = utf8.size();
		TextBoundaries result;
		result.Grapheme.assign(n + 1, 0);
		result.Word.assign(n + 1, 0);
		result.LineBreak.assign(n + 1, LineBreakKind::None);
		result.Grapheme[0] = result.Grapheme[n] = 1;
		result.Word[0] = result.Word[n] = 1;
		const auto graphemes = Breaks(utf8, language, set_graphemebreaks_utf8);
		const auto words = Breaks(utf8, language, set_wordbreaks_utf8);
		const auto lines = Breaks(utf8, language, set_linebreaks_utf8);
		for (std::size_t i = 0; i + 1 < n; ++i)
		{
			result.Grapheme[i + 1] = graphemes[i] == GRAPHEMEBREAK_BREAK ? 1 : 0;
			result.Word[i + 1] = words[i] == WORDBREAK_BREAK ? 1 : 0;
			result.LineBreak[i + 1] = lines[i] == LINEBREAK_MUSTBREAK ? LineBreakKind::Mandatory
				: lines[i] == LINEBREAK_ALLOWBREAK					  ? LineBreakKind::Allowed
																	  : LineBreakKind::None;
		}
		// The end of text is always a (mandatory) break; a text ending with a newline
		// then starts an empty final line, which layout handles separately.
		if (n > 0)
		{
			result.LineBreak[n] = LineBreakKind::Mandatory;
		}
		return result;
	}

	std::vector<ScriptSpan> FindScriptSpans(std::string_view utf8)
	{
		RequireSegmentable(utf8);
		std::vector<ScriptSpan> spans;
		if (utf8.empty())
		{
			return spans;
		}
		const SBCodepointSequence sequence{ SBStringEncodingUTF8, utf8.data(), utf8.size() };
		const SBScriptLocatorRef locator = SBScriptLocatorCreate();
		if (!locator)
		{
			throw std::bad_alloc();
		}
		SBScriptLocatorLoadCodepoints(locator, &sequence);
		while (SBScriptLocatorMoveNext(locator))
		{
			const auto* agent = SBScriptLocatorGetAgent(locator);
			ScriptSpan span;
			span.Begin = static_cast<std::uint32_t>(agent->offset);
			span.End = static_cast<std::uint32_t>(agent->offset + agent->length);
			span.Tag = SBScriptGetUnicodeTag(agent->script);
			if (!spans.empty() && spans.back().Tag == span.Tag && spans.back().End == span.Begin)
			{
				spans.back().End = span.End;
			}
			else
			{
				spans.push_back(span);
			}
		}
		SBScriptLocatorRelease(locator);
		return spans;
	}

	std::vector<BidiParagraph> AnalyzeBidi(std::string_view utf8, TextDirection direction)
	{
		RequireSegmentable(utf8);
		if (direction != TextDirection::Auto && direction != TextDirection::LeftToRight && direction != TextDirection::RightToLeft)
		{
			throw std::invalid_argument("Invalid paragraph direction");
		}
		const SBLevel requested = direction == TextDirection::LeftToRight ? SBLevel(0)
			: direction == TextDirection::RightToLeft					  ? SBLevel(1)
																		  : SBLevel(SBLevelDefaultLTR);
		std::vector<BidiParagraph> paragraphs;
		const std::size_t n = utf8.size();
		if (n > 0)
		{
			const SBCodepointSequence sequence{ SBStringEncodingUTF8, utf8.data(), n };
			std::unique_ptr<const _SBAlgorithm, AlgorithmDeleter> algorithm(SBAlgorithmCreate(&sequence));
			if (!algorithm)
			{
				throw std::bad_alloc();
			}
			for (std::size_t offset = 0; offset < n;)
			{
				SBUInteger length = 0;
				SBUInteger separator = 0;
				SBAlgorithmGetParagraphBoundary(algorithm.get(), offset, n - offset, &length, &separator);
				std::unique_ptr<const _SBParagraph, ParagraphDeleter> paragraph(
					SBAlgorithmCreateParagraph(algorithm.get(), offset, length, requested));
				if (!paragraph || length == 0)
				{
					throw std::runtime_error("SheenBidi could not analyze the paragraph");
				}
				BidiParagraph result;
				result.Begin = static_cast<std::uint32_t>(offset);
				result.End = static_cast<std::uint32_t>(offset + length - separator);
				result.SeparatorLength = static_cast<std::uint32_t>(separator);
				result.BaseLevel = SBParagraphGetBaseLevel(paragraph.get());
				const SBLevel* levels = SBParagraphGetLevelsPtr(paragraph.get());
				result.Levels.assign(levels, levels + (length - separator));
				paragraphs.push_back(std::move(result));
				offset += length;
			}
		}
		if (n == 0 || paragraphs.back().SeparatorLength > 0)
		{
			BidiParagraph empty;
			empty.Begin = empty.End = static_cast<std::uint32_t>(n);
			empty.BaseLevel = direction == TextDirection::RightToLeft ? 1
				: direction == TextDirection::LeftToRight			  ? 0
				: paragraphs.empty()								  ? 0
																	  : paragraphs.back().BaseLevel;
			paragraphs.push_back(std::move(empty));
		}
		return paragraphs;
	}

	bool IsHangingWhitespace(char32_t c)
	{
		return c == 0x20 || c == 0x09 || c == 0x1680 || (c >= 0x2000 && c <= 0x2006) || (c >= 0x2008 && c <= 0x200A) || c == 0x205F ||
			c == 0x3000;
	}

	bool IsLineSeparator(char32_t c)
	{
		return c == 0x0B || c == 0x0C || c == 0x2028;
	}

	std::uint32_t MakeScriptTag(std::string_view code)
	{
		if (code.size() != 4)
		{
			return 0;
		}
		return (std::uint32_t(std::uint8_t(code[0])) << 24) | (std::uint32_t(std::uint8_t(code[1])) << 16) |
			(std::uint32_t(std::uint8_t(code[2])) << 8) | std::uint32_t(std::uint8_t(code[3]));
	}

	std::string ScriptTagToString(std::uint32_t tag)
	{
		if (tag == 0)
		{
			return {};
		}
		return { char(tag >> 24), char((tag >> 16) & 0xFF), char((tag >> 8) & 0xFF), char(tag & 0xFF) };
	}
} // namespace Swim::Text
