#pragma once

#include <memory>
#include <string>
#include <vector>
#include <Engine/Systems/Renderer/Core/Font/FontData.h>
#include "Library/glm/vec2.hpp"
#include "Library/glm/vec4.hpp"

namespace Engine
{

	enum class TextAllignemt : uint8_t
	{
		Left, Right, Center, Justified
	};

	struct TextComponent
	{

		glm::vec4 fillColor = glm::vec4(1.0f);   // Default: white
		glm::vec4 strokeColor = glm::vec4(0.0f); // Default: black
		float strokeWidth = 0.0f; // Default: no stroke

		const std::string& GetText() const { return text; }
		const std::shared_ptr<FontInfo>& GetFont() const { return font; }
		TextAllignemt GetAlignment() const { return alignment; }

		void SetText(const std::string& newText)
		{
			if (text != newText)
			{
				text = newText;
				// Changing text affects UTF, which affects lines, which affects widths
				utfDirty = true;
				linesDirty = true;
				lineWidthsDirty = true;
			}
		}

		void SetFont(std::shared_ptr<FontInfo> newFont)
		{
			if (font != newFont)
			{
				font = std::move(newFont);
				// Only widths depend on the font metrics
				lineWidthsDirty = true;
			}
		}

		void SetAlignment(TextAllignemt newAlign)
		{
			if (alignment != newAlign)
			{
				alignment = newAlign;
				// Alignment does not change cached geometry data
				// so no dirty flags needed here
			}
		}

		const std::u32string& GetUtf32()
		{
			if (utfDirty) { RebuildUtf(); }
			return utf32Text;
		}

		const std::vector<std::u32string>& GetLines()
		{
			// Ensure UTF is current before building lines
			if (utfDirty) { RebuildUtf(); }
			if (linesDirty) { RebuildLines(); }
			return lines;
		}

		const std::vector<float>& GetLineWidths()
		{
			// Ensure lines are current before measuring widths
			if (utfDirty) { RebuildUtf(); }
			if (linesDirty) { RebuildLines(); }
			if (lineWidthsDirty) { RebuildWidths(); }
			return lineWidths;
		}

	private:

		std::string text;
		std::shared_ptr<FontInfo> font = nullptr;
		TextAllignemt alignment = TextAllignemt::Left;

		std::u32string utf32Text;
		std::vector<std::u32string> lines;
		std::vector<float> lineWidths;

		bool utfDirty = true;
		bool linesDirty = true;
		bool lineWidthsDirty = true;

		// handles 3 byte codes, but not 4 byte codes for all the weird crazy emojis
		static std::u32string Utf8ToUtf32(const std::string& s)
		{
			std::u32string out;
			out.reserve(s.size());
			for (size_t i = 0; i < s.size();)
			{
				unsigned char c = (unsigned char)s[i];
				if ((c & 0x80) == 0x00)
				{
					out.push_back(c); i += 1;
				}
				else if ((c & 0xE0) == 0xC0 && i + 1 < s.size())
				{
					out.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2;
				}
				else if ((c & 0xF0) == 0xE0 && i + 2 < s.size())
				{
					out.push_back(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F));
					i += 3;
				}
				else
				{
					i += 1; // Skip invalid
				}
			}
			return out;
		}

		static std::vector<std::u32string> SplitLines(const std::u32string& s)
		{
			std::vector<std::u32string> lines;
			std::u32string cur;

			for (char32_t ch : s)
			{
				if (ch == U'\n')
				{
					lines.push_back(cur);
					cur.clear();
				}
				else
				{
					cur.push_back(ch);
				}
			}

			lines.push_back(cur);
			return lines;
		}

		static float MeasureEm(const std::u32string& line, const FontInfo& fi)
		{
			auto getKerning = [&](uint32_t l, uint32_t r)->float
			{
				auto it = fi.kerning.find(FontInfo::PackKerningKey(l, r));
				return (it == fi.kerning.end()) ? 0.0f : it->second;
			};

			float w = 0.0f;
			for (size_t i = 0; i < line.size(); ++i)
			{
				auto ig = fi.glyphs.find(line[i]);
				if (ig == fi.glyphs.end()) { continue; }
				w += ig->second.advance;
				if (i + 1 < line.size()) { w += getKerning(line[i], line[i + 1]); }
			}

			return w;
		}

		void RebuildUtf()
		{
			utf32Text = Utf8ToUtf32(text);
			utfDirty = false;

			// Changing UTF means lines and widths need refresh
			linesDirty = true;
			lineWidthsDirty = true;
		}

		void RebuildLines()
		{
			// Safety: ensure utf is fresh before splitting
			if (utfDirty) { RebuildUtf(); }
			lines = SplitLines(utf32Text);
			linesDirty = false;
			lineWidthsDirty = true;
		}

		void RebuildWidths()
		{
			lineWidths.clear();
			if (font)
			{
				lineWidths.reserve(lines.size());
				for (auto& l : lines)
				{
					lineWidths.push_back(MeasureEm(l, *font));
				}
			}
			lineWidthsDirty = false;
		}

	};

} // Namespace Engine
