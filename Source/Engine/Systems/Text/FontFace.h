#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Swim::Text
{
	enum class TextDirection : std::uint8_t
	{
		Auto,
		LeftToRight,
		RightToLeft
	};

	struct ShapeOptions
	{
		TextDirection Direction = TextDirection::Auto;
		std::string_view Script;   // ISO 15924, for example "Arab". Empty guesses from the run.
		std::string_view Language; // BCP 47. Empty uses "und", never the process locale.
		bool Kerning = true;
		bool Ligatures = true;
	};

	struct FontMetrics
	{
		float Ascender = 0.0f;
		float Descender = 0.0f; // Negative below the baseline.
		float LineHeight = 0.0f;
	};

	struct ShapedGlyph
	{
		std::uint32_t Glyph = 0;
		std::uint32_t Cluster = 0; // Byte offset in the original UTF-8 run, not a code point index.
		float AdvanceX = 0.0f;
		float AdvanceY = 0.0f;
		float OffsetX = 0.0f;
		float OffsetY = 0.0f; // Font coordinates: positive up.
	};

	struct ShapedRun
	{
		std::vector<ShapedGlyph> Glyphs; // Visual order from HarfBuzz.
		FontMetrics Metrics;
		TextDirection Direction = TextDirection::LeftToRight;
		float AdvanceX = 0.0f;
		std::uint32_t MissingGlyphs = 0;
	};

	class GlyphAtlas;

	// Owns a copy of the font bytes. No filesystem, renderer, global library or
	// platform font dependency. Const operations are safe to call from jobs.
	class FontFace final
	{
	  public:
		explicit FontFace(std::span<const std::byte> bytes, std::uint32_t faceIndex = 0);
		~FontFace();
		FontFace(const FontFace&) = delete;
		FontFace& operator=(const FontFace&) = delete;

		FontMetrics GetMetrics(float size) const;
		std::uint32_t GetGlyph(char32_t codePoint) const;
		// Shapes one horizontal script/direction run. The caller owns paragraph
		// bidi segmentation and fallback. Invalid UTF-8 becomes U+FFFD in HarfBuzz.
		ShapedRun Shape(std::string_view utf8, float size, const ShapeOptions& options = {}) const;

	  private:
		friend class GlyphAtlas;
		struct Impl;

		struct GlyphBitmap
		{
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			float Left = 0.0f;
			float Top = 0.0f;
			std::vector<std::uint8_t> Pixels; // Top-down RGB, linear distance values.
		};

		GlyphBitmap Rasterize(std::uint32_t glyph, float emSize, float range, std::uint32_t maxDimension) const;
		std::unique_ptr<Impl> impl;
	};
} // namespace Swim::Text
