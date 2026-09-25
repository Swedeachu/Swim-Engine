#include "Engine/Systems/Text/TextLayout.h"
#include "Engine/Systems/Text/Utf8.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Swim::Text
{
	std::uint32_t TextLayout::ClampOffset(std::uint32_t offset) const
	{
		offset = std::min<std::uint32_t>(offset, static_cast<std::uint32_t>(text.size()));
		while (offset > 0 && (boundaries.Grapheme.empty() || !boundaries.Grapheme[offset]))
		{
			--offset;
		}
		return offset;
	}

	bool TextLayout::IsCaretStop(std::uint32_t offset) const
	{
		return offset <= text.size() && !boundaries.Grapheme.empty() && boundaries.Grapheme[offset] != 0;
	}

	std::uint32_t TextLayout::GetLineIndex(std::uint32_t offset) const
	{
		if (lines.empty())
		{
			return 0;
		}
		offset = ClampOffset(offset);
		// The last line that begins at or before the offset: a soft break belongs to the
		// following line (downstream affinity); a hard break's separator to the line before.
		const auto it = std::upper_bound(lines.begin(), lines.end(), offset,
			[](std::uint32_t value, const TextLine& line)
			{
				return value < line.Begin;
			});
		return it == lines.begin() ? 0u : static_cast<std::uint32_t>(it - lines.begin() - 1);
	}

	const TextLayout::Grapheme* TextLayout::FindGrapheme(std::uint32_t offset) const
	{
		const auto it = std::lower_bound(graphemes.begin(), graphemes.end(), offset,
			[](const Grapheme& grapheme, std::uint32_t value)
			{
				return grapheme.Begin < value;
			});
		return it != graphemes.end() && it->Begin == offset ? &*it : nullptr;
	}

	float TextLayout::CaretX(std::uint32_t lineIndex, std::uint32_t offset) const
	{
		const auto& line = lines[lineIndex];
		// Attach to the character after the caret (its leading edge), else to the one
		// before it (its trailing edge). At a direction boundary the two edges differ;
		// the leading character wins.
		if (offset < line.End)
		{
			if (const auto* g = FindGrapheme(offset); g && g->Line == lineIndex)
			{
				return g->RightToLeft ? g->Right : g->Left;
			}
		}
		if (offset > line.Begin)
		{
			const auto it = std::lower_bound(graphemes.begin(), graphemes.end(), offset,
				[](const Grapheme& grapheme, std::uint32_t value)
				{
					return grapheme.Begin < value;
				});
			if (it != graphemes.begin())
			{
				const auto& before = *std::prev(it);
				if (before.Line == lineIndex)
				{
					return before.RightToLeft ? before.Left : before.Right;
				}
			}
		}
		return (line.BaseLevel & 1) != 0 ? line.X + line.Width : line.X;
	}

	TextCaret TextLayout::GetCaret(std::uint32_t offset) const
	{
		TextCaret caret;
		if (lines.empty())
		{
			return caret;
		}
		offset = ClampOffset(offset);
		caret.Line = GetLineIndex(offset);
		const auto& line = lines[caret.Line];
		offset = std::clamp(offset, line.Begin, line.End);
		caret.X = CaretX(caret.Line, offset);
		caret.Top = line.Top;
		caret.Height = line.Height;
		const auto* g = offset < line.End ? FindGrapheme(offset) : nullptr;
		caret.RightToLeft = g ? g->RightToLeft : (line.BaseLevel & 1) != 0;
		return caret;
	}

	std::uint32_t TextLayout::HitTestLine(std::uint32_t lineIndex, float x) const
	{
		const auto& line = lines[lineIndex];
		// A soft-wrapped line's end offset is displayed on the next line, so it is not a
		// candidate here; its trailing space's start is.
		const std::uint32_t last = line.HardBreak || lineIndex + 1 == lines.size() ? line.End : ClampOffset(line.End - 1);
		std::uint32_t best = line.Begin;
		float bestDistance = std::numeric_limits<float>::infinity();
		for (std::uint32_t offset = line.Begin;; offset = NextCaretStop(offset))
		{
			const float distance = std::abs(CaretX(lineIndex, offset) - x);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = offset;
			}
			if (offset >= last || offset >= text.size())
			{
				break;
			}
		}
		return std::min(best, std::max(last, line.Begin));
	}

	std::uint32_t TextLayout::HitTest(float x, float y) const
	{
		if (lines.empty())
		{
			return 0;
		}
		std::uint32_t lineIndex = static_cast<std::uint32_t>(lines.size() - 1);
		for (std::uint32_t i = 0; i < lines.size(); ++i)
		{
			if (y < lines[i].Top + lines[i].Height)
			{
				lineIndex = i;
				break;
			}
		}
		return HitTestLine(lineIndex, x);
	}

	std::vector<TextRect> TextLayout::GetSelectionRects(std::uint32_t begin, std::uint32_t end) const
	{
		std::vector<TextRect> rects;
		begin = ClampOffset(begin);
		end = ClampOffset(end);
		if (begin > end)
		{
			std::swap(begin, end);
		}
		if (begin == end)
		{
			return rects;
		}
		for (std::uint32_t lineIndex = GetLineIndex(begin); lineIndex < lines.size(); ++lineIndex)
		{
			const auto& line = lines[lineIndex];
			if (line.Begin >= end && lineIndex != GetLineIndex(begin))
			{
				break;
			}
			std::vector<std::pair<float, float>> spans;
			for (auto it = std::lower_bound(graphemes.begin(), graphemes.end(), std::max(begin, line.Begin),
					 [](const Grapheme& grapheme, std::uint32_t value)
					 {
						 return grapheme.Begin < value;
					 });
				 it != graphemes.end() && it->Begin < std::min(end, line.End); ++it)
			{
				if (it->Line == lineIndex && it->Right > it->Left)
				{
					spans.emplace_back(it->Left, it->Right);
				}
			}
			std::sort(spans.begin(), spans.end());
			for (const auto& [left, right] : spans)
			{
				if (!rects.empty() && rects.back().Y == line.Top && std::abs(rects.back().X + rects.back().Width - left) < 1e-3f)
				{
					rects.back().Width = right - rects.back().X;
				}
				else
				{
					rects.push_back({ left, line.Top, right - left, line.Height });
				}
			}
		}
		return rects;
	}

	std::uint32_t TextLayout::NextCaretStop(std::uint32_t offset) const
	{
		offset = ClampOffset(offset);
		if (offset >= text.size())
		{
			return static_cast<std::uint32_t>(text.size());
		}
		do
		{
			++offset;
		} while (offset < text.size() && !boundaries.Grapheme[offset]);
		return offset;
	}

	std::uint32_t TextLayout::PreviousCaretStop(std::uint32_t offset) const
	{
		offset = ClampOffset(offset);
		if (offset == 0)
		{
			return 0;
		}
		return ClampOffset(offset - 1);
	}

	std::uint32_t TextLayout::NextWord(std::uint32_t offset) const
	{
		const auto n = static_cast<std::uint32_t>(text.size());
		const auto nextBoundary = [&](std::uint32_t from)
		{
			do
			{
				++from;
			} while (from < n && !boundaries.Word[from]);
			return std::min(from, n);
		};
		offset = ClampOffset(offset);
		if (offset >= n)
		{
			return n;
		}
		std::uint32_t result = nextBoundary(offset);
		while (result < n && IsHangingWhitespace(DecodeUtf8(text, result).CodePoint))
		{
			result = nextBoundary(result);
		}
		return result;
	}

	std::uint32_t TextLayout::PreviousWord(std::uint32_t offset) const
	{
		const auto previousBoundary = [&](std::uint32_t from)
		{
			do
			{
				--from;
			} while (from > 0 && !boundaries.Word[from]);
			return from;
		};
		offset = ClampOffset(offset);
		if (offset == 0)
		{
			return 0;
		}
		std::uint32_t result = previousBoundary(offset);
		while (result > 0 && IsHangingWhitespace(DecodeUtf8(text, result).CodePoint))
		{
			result = previousBoundary(result);
		}
		return result;
	}

	std::uint32_t TextLayout::LineStart(std::uint32_t offset) const
	{
		return lines.empty() ? 0u : lines[GetLineIndex(offset)].Begin;
	}

	std::uint32_t TextLayout::LineEnd(std::uint32_t offset) const
	{
		if (lines.empty())
		{
			return 0;
		}
		const auto index = GetLineIndex(offset);
		const auto& line = lines[index];
		if (line.HardBreak || index + 1 == lines.size() || line.End == line.Begin)
		{
			return line.End;
		}
		// A soft line's end is displayed on the next line; stop before its hanging space.
		return std::max(line.Begin, PreviousCaretStop(line.End));
	}

	std::uint32_t TextLayout::LineAbove(std::uint32_t offset, float x) const
	{
		const auto index = GetLineIndex(offset);
		return lines.empty() || index == 0 ? ClampOffset(offset) : HitTestLine(index - 1, x);
	}

	std::uint32_t TextLayout::LineBelow(std::uint32_t offset, float x) const
	{
		const auto index = GetLineIndex(offset);
		return lines.empty() || index + 1 >= lines.size() ? ClampOffset(offset) : HitTestLine(index + 1, x);
	}
} // namespace Swim::Text
