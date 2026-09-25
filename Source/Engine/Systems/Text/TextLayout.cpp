#include "Engine/Systems/Text/TextLayout.h"
#include "Engine/Systems/Text/Utf8.h"

#include <SheenBidi/SheenBidi.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>

namespace Swim::Text
{
	namespace
	{
		constexpr std::size_t MaxLayoutBytes = 1024u * 1024u;

		struct AlgorithmDeleter
		{
			void operator()(const _SBAlgorithm* algorithm) const { SBAlgorithmRelease(algorithm); }
		};

		struct ParagraphDeleter
		{
			void operator()(const _SBParagraph* paragraph) const { SBParagraphRelease(paragraph); }
		};

		struct LineDeleter
		{
			void operator()(const _SBLine* line) const { SBLineRelease(line); }
		};

		void ValidateDesc(const TextLayoutDesc& desc)
		{
			const bool valid = std::isfinite(desc.Size) && desc.Size > 0.0f && desc.Size <= 16384.0f && !std::isnan(desc.MaxWidth) &&
				desc.MaxWidth > 0.0f && std::isfinite(desc.LineSpacing) && desc.LineSpacing >= 0.25f && desc.LineSpacing <= 8.0f &&
				desc.Language.size() <= 128 && static_cast<std::uint8_t>(desc.Align) <= static_cast<std::uint8_t>(TextAlign::Center) &&
				static_cast<std::uint8_t>(desc.Wrap) <= static_cast<std::uint8_t>(TextWrap::Word) &&
				(desc.Direction == TextDirection::Auto || desc.Direction == TextDirection::LeftToRight ||
					desc.Direction == TextDirection::RightToLeft);
			if (!valid)
			{
				throw std::invalid_argument("Invalid text layout description");
			}
		}
	} // namespace

	// Builds one TextLayout. Keeps per-byte face/script tables and the paragraph's
	// measured grapheme advances between the measuring and the line passes.
	struct TextLayoutBuilder
	{
		TextLayout& L;
		const std::string& Text;
		const FontCollection& Fonts;
		const TextLayoutDesc& Desc;
		std::vector<std::uint32_t> FaceAt;								// Per byte (constant within a grapheme).
		std::vector<std::uint32_t> ScriptAt;							// Per byte.
		std::vector<float> Advance;										// Per byte: measured advance of the grapheme starting there.
		std::vector<std::pair<std::size_t, std::size_t>> LineGraphemes; // Per line: [first, last) in L.graphemes.
		float Y = 0.0f;

		explicit TextLayoutBuilder(TextLayout& layout)
			: L(layout), Text(layout.text), Fonts(*layout.fonts), Desc(layout.desc), FaceAt(layout.text.size() + 1, 0),
			  ScriptAt(layout.text.size() + 1, 0), Advance(layout.text.size() + 1, 0.0f)
		{
		}

		const std::vector<std::uint8_t>& Graphemes() const { return L.boundaries.Grapheme; }

		std::uint32_t NextGrapheme(std::uint32_t offset) const
		{
			const auto& g = Graphemes();
			std::uint32_t next = offset + 1;
			while (next < Text.size() && !g[next])
			{
				++next;
			}
			return std::min<std::uint32_t>(next, static_cast<std::uint32_t>(Text.size()));
		}

		std::uint32_t GraphemeStartOf(std::uint32_t offset) const
		{
			const auto& g = Graphemes();
			while (offset > 0 && !g[offset])
			{
				--offset;
			}
			return offset;
		}

		char32_t CodePointAt(std::uint32_t offset) const { return DecodeUtf8(Text, offset).CodePoint; }

		void AssignScripts()
		{
			for (const auto& span : FindScriptSpans(Text))
			{
				const std::uint32_t tag =
					span.Tag == MakeScriptTag("Zyyy") || span.Tag == MakeScriptTag("Zinh") || span.Tag == MakeScriptTag("Zzzz") ? 0
																																: span.Tag;
				std::fill(ScriptAt.begin() + span.Begin, ScriptAt.begin() + span.End, tag);
			}
		}

		// Font fallback per grapheme cluster; neutral clusters keep the previous face.
		void AssignFaces()
		{
			std::vector<char32_t> cluster;
			std::uint32_t previous = FontCollection::NoPreference;
			for (std::uint32_t begin = 0; begin < Text.size();)
			{
				const std::uint32_t end = NextGrapheme(begin);
				cluster.clear();
				for (std::uint32_t offset = begin; offset < end;)
				{
					const auto decoded = DecodeUtf8(Text, offset);
					cluster.push_back(decoded.CodePoint);
					offset += decoded.Length;
				}
				const bool separator = cluster.size() == 1 && (cluster[0] == U'\n' || cluster[0] == U'\r');
				const std::uint32_t face = Fonts.SelectFace(cluster, separator ? FontCollection::NoPreference : previous);
				std::fill(FaceAt.begin() + begin, FaceAt.begin() + end, face);
				previous = separator ? FontCollection::NoPreference : face;
				begin = end;
			}
		}

		ShapedRun Shape(std::uint32_t begin, std::uint32_t end, std::uint32_t face, std::uint32_t script, std::uint8_t level) const
		{
			ShapeOptions options;
			options.Direction = (level & 1) != 0 ? TextDirection::RightToLeft : TextDirection::LeftToRight;
			const std::string scriptCode = ScriptTagToString(script);
			options.Script = scriptCode;
			options.Language = Desc.Language;
			options.Kerning = Desc.Kerning;
			options.Ligatures = Desc.Ligatures;
			auto run = Fonts.GetFace(face)->Shape(std::string_view(Text).substr(begin, end - begin), Desc.Size, options);
			for (auto& glyph : run.Glyphs)
			{
				glyph.Cluster += begin;
			}
			return run;
		}

		// Calls emit(begin, end) for the maximal subranges of [begin, end) with one face,
		// script and (when levels is given) level. Splits only at grapheme starts.
		template <typename Emit>
		void Itemize(std::uint32_t begin, std::uint32_t end, const std::uint8_t* levels, std::uint32_t levelBase, Emit emit) const
		{
			std::uint32_t start = begin;
			for (std::uint32_t offset = begin; offset < end; offset = NextGrapheme(offset))
			{
				if (offset == start)
				{
					continue;
				}
				const bool split = FaceAt[offset] != FaceAt[start] || ScriptAt[offset] != ScriptAt[start] ||
					(levels && levels[offset - levelBase] != levels[start - levelBase]);
				if (split)
				{
					emit(start, offset);
					start = offset;
				}
			}
			if (start < end)
			{
				emit(start, end);
			}
		}

		// Measuring pass: per-grapheme advances of a paragraph shaped with its resolved levels.
		void Measure(const BidiParagraph& paragraph)
		{
			Itemize(paragraph.Begin, paragraph.End, paragraph.Levels.data(), paragraph.Begin,
				[&](std::uint32_t begin, std::uint32_t end)
				{
					const auto run = Shape(begin, end, FaceAt[begin], ScriptAt[begin], paragraph.Levels[begin - paragraph.Begin]);
					for (const auto& glyph : run.Glyphs)
					{
						Advance[GraphemeStartOf(std::clamp(glyph.Cluster, begin, end - 1))] += glyph.AdvanceX;
					}
				});
		}

		struct LineRange
		{
			std::uint32_t Begin = 0;
			std::uint32_t End = 0; // Before any hard-break character.
			std::uint32_t Next = 0;
			bool Hard = false;
		};

		// Greedy UAX #14 breaking over measured grapheme advances. Trailing hanging
		// whitespace does not count toward the width; a grapheme that would overflow an
		// otherwise empty-of-opportunities line breaks before it (emergency break).
		std::vector<LineRange> BreakLines(const BidiParagraph& paragraph) const
		{
			std::vector<LineRange> result;
			const auto& breaks = L.boundaries.LineBreak;
			const bool wrap = Desc.Wrap == TextWrap::Word && std::isfinite(Desc.MaxWidth);
			std::uint32_t lineStart = paragraph.Begin;
			float width = 0.0f;			 // Advances of [lineStart, offset).
			std::uint32_t lastBreak = 0; // 0 = none (a break at 0 is never inside a line).
			float widthAtBreak = 0.0f;
			const auto emit = [&](std::uint32_t end, std::uint32_t next, bool hard)
			{
				std::uint32_t contentEnd = end;
				if (contentEnd > lineStart)
				{
					const auto last = static_cast<std::uint32_t>(PreviousUtf8(Text, contentEnd));
					if (IsLineSeparator(CodePointAt(last)))
					{
						contentEnd = last;
					}
				}
				result.push_back({ lineStart, contentEnd, next, hard });
				lineStart = next;
			};
			for (std::uint32_t offset = paragraph.Begin; offset < paragraph.End; offset = NextGrapheme(offset))
			{
				if (offset > lineStart && breaks[offset] == LineBreakKind::Mandatory)
				{
					emit(offset, offset, true);
					width = 0.0f;
					lastBreak = 0;
				}
				if (offset > lineStart && breaks[offset] == LineBreakKind::Allowed)
				{
					lastBreak = offset;
					widthAtBreak = width;
				}
				const float advance = Advance[offset];
				if (wrap && !IsHangingWhitespace(CodePointAt(offset)))
				{
					while (offset > lineStart && width + advance > Desc.MaxWidth)
					{
						if (lastBreak > lineStart)
						{
							const std::uint32_t at = lastBreak;
							emit(at, at, false);
							width -= widthAtBreak;
							lastBreak = 0;
						}
						else
						{
							emit(offset, offset, false);
							width = 0.0f;
						}
					}
				}
				width += advance;
			}
			emit(paragraph.End, paragraph.End + paragraph.SeparatorLength, true);
			return result;
		}

		struct FaceMetrics
		{
			float Ascent = 0.0f;
			float Descent = 0.0f;
			float LineHeight = 0.0f;
		};

		void Include(FaceMetrics& metrics, std::uint32_t face) const
		{
			const auto m = Fonts.GetFace(face)->GetMetrics(Desc.Size);
			metrics.Ascent = std::max(metrics.Ascent, m.Ascender);
			metrics.Descent = std::max(metrics.Descent, -m.Descender);
			metrics.LineHeight = std::max(metrics.LineHeight, m.LineHeight);
		}

		// Visual extents of the graphemes of one shaped piece. A cluster spanning several
		// graphemes (a ligature) is split evenly between them in reading direction.
		void RecordGraphemes(const ShapedRun& run, std::uint32_t begin, std::uint32_t end, float penStart, bool rtl, std::uint32_t line)
		{
			std::map<std::uint32_t, std::pair<float, float>> clusters; // Grapheme start -> [left, right].
			float pen = penStart;
			for (const auto& glyph : run.Glyphs)
			{
				const std::uint32_t start = GraphemeStartOf(std::clamp(glyph.Cluster, begin, end - 1));
				auto [it, inserted] = clusters.try_emplace(start, pen, pen + glyph.AdvanceX);
				if (!inserted)
				{
					it->second.first = std::min(it->second.first, pen);
					it->second.second = std::max(it->second.second, pen + glyph.AdvanceX);
				}
				pen += glyph.AdvanceX;
			}
			for (auto it = clusters.begin(); it != clusters.end(); ++it)
			{
				const auto next = std::next(it);
				const std::uint32_t clusterEnd = next == clusters.end() ? end : next->first;
				std::vector<std::uint32_t> starts;
				for (std::uint32_t offset = it->first; offset < clusterEnd; offset = NextGrapheme(offset))
				{
					starts.push_back(offset);
				}
				const float left = it->second.first;
				const float step = (it->second.second - left) / float(starts.size());
				for (std::size_t i = 0; i < starts.size(); ++i)
				{
					const std::size_t slot = rtl ? starts.size() - 1 - i : i;
					L.graphemes.push_back(
						{ starts[i], NextGrapheme(starts[i]), line, left + step * float(slot), left + step * float(slot + 1), rtl });
				}
			}
			// Every grapheme from the first cluster on is covered above (clusters merged by
			// HarfBuzz split their range); any before it, or a piece without glyphs, gets a
			// zero-width stop at the piece's pen start.
			const std::uint32_t firstCovered = clusters.empty() ? end : clusters.begin()->first;
			for (std::uint32_t offset = begin; offset < firstCovered; offset = NextGrapheme(offset))
			{
				L.graphemes.push_back({ offset, NextGrapheme(offset), line, penStart, penStart, rtl });
			}
		}

		void EmitLine(const LineRange& range, const _SBParagraph* paragraph, std::uint8_t baseLevel)
		{
			TextLine line;
			line.Begin = range.Begin;
			line.End = range.End;
			line.Next = range.Next;
			line.BaseLevel = baseLevel;
			line.HardBreak = range.Hard;
			line.FirstRun = static_cast<std::uint32_t>(L.runs.size());
			const auto lineIndex = static_cast<std::uint32_t>(L.lines.size());
			const std::size_t firstGrapheme = L.graphemes.size();
			FaceMetrics metrics;
			Include(metrics, 0);
			float pen = 0.0f;
			if (range.End > range.Begin)
			{
				std::unique_ptr<const _SBLine, LineDeleter> sbLine(SBParagraphCreateLine(paragraph, range.Begin, range.End - range.Begin));
				if (!sbLine)
				{
					throw std::runtime_error("SheenBidi could not create a line");
				}
				const SBRun* sbRuns = SBLineGetRunsPtr(sbLine.get());
				const SBUInteger count = SBLineGetRunCount(sbLine.get());
				for (SBUInteger r = 0; r < count; ++r)
				{
					const auto begin = static_cast<std::uint32_t>(sbRuns[r].offset);
					const auto end = static_cast<std::uint32_t>(sbRuns[r].offset + sbRuns[r].length);
					const std::uint8_t level = sbRuns[r].level;
					std::vector<std::pair<std::uint32_t, std::uint32_t>> pieces;
					Itemize(begin, end, nullptr, 0,
						[&](std::uint32_t b, std::uint32_t e)
						{
							pieces.emplace_back(b, e);
						});
					if ((level & 1) != 0)
					{
						std::reverse(pieces.begin(), pieces.end());
					}
					for (const auto& [b, e] : pieces)
					{
						const auto shaped = Shape(b, e, FaceAt[b], ScriptAt[b], level);
						TextRun run;
						run.Begin = b;
						run.End = e;
						run.Face = FaceAt[b];
						run.Script = ScriptAt[b];
						run.Level = level;
						run.X = pen;
						run.FirstGlyph = static_cast<std::uint32_t>(L.glyphs.size());
						RecordGraphemes(shaped, b, e, pen, (level & 1) != 0, lineIndex);
						for (const auto& glyph : shaped.Glyphs)
						{
							L.glyphs.push_back(
								{ glyph.Glyph, run.Face, glyph.Cluster, pen + glyph.OffsetX, -glyph.OffsetY, glyph.AdvanceX });
							pen += glyph.AdvanceX;
						}
						L.missingGlyphs += shaped.MissingGlyphs;
						run.GlyphCount = static_cast<std::uint32_t>(L.glyphs.size()) - run.FirstGlyph;
						run.Width = pen - run.X;
						Include(metrics, run.Face);
						L.runs.push_back(run);
					}
				}
			}
			line.RunCount = static_cast<std::uint32_t>(L.runs.size()) - line.FirstRun;
			// Trailing hanging whitespace takes the paragraph level (L1) and hangs at the
			// paragraph's end: to the right in LTR paragraphs, to the left in RTL ones.
			float trailing = 0.0f;
			for (std::uint32_t offset = range.End; offset > range.Begin;)
			{
				const std::uint32_t start = GraphemeStartOf(static_cast<std::uint32_t>(PreviousUtf8(Text, offset)));
				if (!IsHangingWhitespace(CodePointAt(start)))
				{
					break;
				}
				for (std::size_t g = firstGrapheme; g < L.graphemes.size(); ++g)
				{
					if (L.graphemes[g].Begin == start)
					{
						trailing += L.graphemes[g].Right - L.graphemes[g].Left;
					}
				}
				offset = start;
			}
			line.Width = std::max(0.0f, pen - trailing);
			line.X = (baseLevel & 1) != 0 ? trailing : 0.0f;
			line.Ascent = metrics.Ascent;
			line.Descent = metrics.Descent;
			line.Height = metrics.LineHeight * Desc.LineSpacing;
			line.Top = Y;
			line.Baseline = Y + metrics.Ascent + (line.Height - metrics.LineHeight) * 0.5f;
			Y += line.Height;
			L.lines.push_back(line);
			LineGraphemes.emplace_back(firstGrapheme, L.graphemes.size());
		}

		// Horizontal alignment against MaxWidth (or the widest line) and baselines.
		void Align()
		{
			float widest = 0.0f;
			for (const auto& line : L.lines)
			{
				widest = std::max(widest, line.Width);
			}
			const float box = std::isfinite(Desc.MaxWidth) ? Desc.MaxWidth : widest;
			for (std::uint32_t index = 0; index < L.lines.size(); ++index)
			{
				auto& line = L.lines[index];
				const bool rtl = (line.BaseLevel & 1) != 0;
				TextAlign align = Desc.Align;
				if (align == TextAlign::Start)
				{
					align = rtl ? TextAlign::Right : TextAlign::Left;
				}
				else if (align == TextAlign::End)
				{
					align = rtl ? TextAlign::Left : TextAlign::Right;
				}
				const float left = align == TextAlign::Left ? 0.0f
					: align == TextAlign::Right				? box - line.Width
															: (box - line.Width) * 0.5f;
				const float shift = left - line.X;
				line.X = left;
				for (std::uint32_t r = line.FirstRun; r < line.FirstRun + line.RunCount; ++r)
				{
					auto& run = L.runs[r];
					run.X += shift;
					for (std::uint32_t g = run.FirstGlyph; g < run.FirstGlyph + run.GlyphCount; ++g)
					{
						L.glyphs[g].X += shift;
						L.glyphs[g].Y += line.Baseline;
					}
				}
				for (std::size_t g = LineGraphemes[index].first; g < LineGraphemes[index].second; ++g)
				{
					L.graphemes[g].Left += shift;
					L.graphemes[g].Right += shift;
				}
			}
			L.width = widest;
			L.height = Y;
		}

		void Run()
		{
			AssignScripts();
			AssignFaces();
			const auto analysis = AnalyzeBidi(Text, Desc.Direction);
			const SBLevel requested = Desc.Direction == TextDirection::LeftToRight ? SBLevel(0)
				: Desc.Direction == TextDirection::RightToLeft					   ? SBLevel(1)
																				   : SBLevel(SBLevelDefaultLTR);
			std::unique_ptr<const _SBAlgorithm, AlgorithmDeleter> algorithm;
			if (!Text.empty())
			{
				const SBCodepointSequence sequence{ SBStringEncodingUTF8, Text.data(), Text.size() };
				algorithm.reset(SBAlgorithmCreate(&sequence));
				if (!algorithm)
				{
					throw std::bad_alloc();
				}
			}
			for (const auto& paragraph : analysis)
			{
				Measure(paragraph);
				std::unique_ptr<const _SBParagraph, ParagraphDeleter> sbParagraph;
				if (paragraph.End > paragraph.Begin)
				{
					sbParagraph.reset(SBAlgorithmCreateParagraph(
						algorithm.get(), paragraph.Begin, paragraph.End - paragraph.Begin + paragraph.SeparatorLength, requested));
					if (!sbParagraph)
					{
						throw std::runtime_error("SheenBidi could not analyze the paragraph");
					}
				}
				for (const auto& range : BreakLines(paragraph))
				{
					EmitLine(range, sbParagraph.get(), paragraph.BaseLevel);
				}
			}
			Align();
			std::sort(L.graphemes.begin(), L.graphemes.end(),
				[](const auto& a, const auto& b)
				{
					return a.Begin < b.Begin;
				});
		}
	};

	TextLayout::TextLayout(std::string_view utf8, std::shared_ptr<const FontCollection> collection, const TextLayoutDesc& description)
		: fonts(std::move(collection)), desc(description)
	{
		if (!fonts)
		{
			throw std::invalid_argument("Text layout needs a font collection");
		}
		ValidateDesc(desc);
		if (utf8.size() > MaxLayoutBytes)
		{
			throw std::length_error("Text layout input exceeds 1 MiB");
		}
		text = SanitizeUtf8(utf8);
		if (text.size() > MaxLayoutBytes)
		{
			throw std::length_error("Text layout input exceeds 1 MiB after UTF-8 replacement");
		}
		Build();
	}

	void TextLayout::Build()
	{
		boundaries = FindTextBoundaries(text, desc.Language);
		TextLayoutBuilder builder(*this);
		builder.Run();
	}
} // namespace Swim::Text
