#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
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

	// An RGBA8 image of a bitmap font: top-down, tightly packed, straight alpha, colour
	// sRGB-encoded (white glyphs are tinted by the text colour; coloured glyphs keep theirs).
	struct BitmapFontPage
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<std::uint8_t> Rgba;
		// Font pixels per texel; 0 derives it from the grid (CellPixels x CellsPerRow / Width),
		// so a 4x larger page of the same grid draws at the same size, with 4x the detail.
		float FontPixelsPerTexel = 0.0f;
	};

	// One glyph given explicitly: a rectangle on a page (overrides the grid cell).
	struct BitmapFontGlyph
	{
		std::uint32_t Page = 0; // Any id passed to LoadPage (ids >= 0x1100 never collide with grid blocks).
		std::uint32_t X = 0;	// Texels.
		std::uint32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::optional<float> Advance;		 // Font pixels; empty: opaque columns + Spacing.
		std::optional<float> BaselineOffset; // Font pixels from the rectangle's top down to the baseline; empty: Baseline.
	};

	// A pixel font. All metrics are in font pixels; a text size of S logical units per em
	// draws one font pixel as S / EmPixels logical units (with IntegerScale, the largest whole
	// number of units, at least 1).
	struct BitmapFontDesc
	{
		// Grid pages: page `block` holds code points [block x 256, block x 256 + 256) in a
		// CellsPerRow x CellsPerRow grid, row-major. 0 disables the grid (explicit glyphs only).
		std::uint32_t CellsPerRow = 16;
		float CellPixels = 8.0f; // Font pixels per cell side.
		float EmPixels = 8.0f;	 // Font pixels per em.
		float Ascent = 7.0f;	 // Above the baseline.
		float Descent = 1.0f;	 // Below the baseline (positive).
		float LineHeight = 9.0f;
		float Baseline = 7.0f; // From a cell's top down to the baseline.
		// Computed advances are the cell's last opaque column + 1, plus Spacing. Cells without
		// an opaque texel are missing (fallback picks another face), except U+0020.
		float Spacing = 1.0f;
		float SpaceAdvance = 4.0f;		 // U+0020 when it has no explicit glyph or opaque texels.
		float BoldOffset = 1.0f;		 // Synthetic bold: drawn again this far right; advances grow by it.
		float ItalicShear = 0.25f;		 // Synthetic italic: x shift per unit of height above the baseline.
		float UnderlineOffset = 1.0f;	 // Top of the underline, below the baseline.
		float StrikethroughOffset = 4.0f; // Top of the strikethrough line, above the baseline.
		float LineThickness = 1.0f;
		bool IntegerScale = true;
		// Lazily called (at most once per id, possibly from a job thread) for grid blocks
		// (code point >> 8) and for the pages explicit glyphs name. Empty: no such page.
		std::function<std::optional<BitmapFontPage>(std::uint32_t page)> LoadPage;
		std::vector<std::pair<char32_t, BitmapFontGlyph>> Glyphs;
	};

	// Synthetic-style and decoration metrics of a face at a size, logical units, relative to
	// the baseline (positive down).
	struct FontStyleMetrics
	{
		float Pixel = 1.0f;			 // One font pixel (bitmap) or size / 16 (vector): shadow offsets.
		float BoldOffset = 1.0f;	 // Synthetic bold offset, also added to each bold glyph's advance.
		float ItalicShear = 0.2f;	 // x shift per unit of height above the baseline.
		float UnderlineTop = 1.0f;	 // Positive: below the baseline.
		float StrikethroughTop = -4.0f;
		float LineThickness = 1.0f;
	};

	// Owns a copy of the font bytes (or a bitmap font's pages). No filesystem, renderer,
	// global library or platform font dependency. Const operations are safe to call from jobs.
	class FontFace final
	{

	  public:

		explicit FontFace(std::span<const std::byte> bytes, std::uint32_t faceIndex = 0);

		// A bitmap (pixel) font. Throws std::invalid_argument for invalid metrics or glyphs.
		explicit FontFace(BitmapFontDesc desc);

		~FontFace();

		FontFace(const FontFace&) = delete;

		FontFace& operator=(const FontFace&) = delete;

		bool IsBitmap() const { return bitmap != nullptr; }

		FontMetrics GetMetrics(float size) const;

		std::uint32_t GetGlyph(char32_t codePoint) const; // Bitmap faces: the code point itself, 0 when missing.

		// Shapes one horizontal script/direction run. The caller owns paragraph
		// bidi segmentation and fallback. Invalid UTF-8 becomes U+FFFD in HarfBuzz.
		ShapedRun Shape(std::string_view utf8, float size, const ShapeOptions& options = {}) const;

		// The em size atlas quads of this face are scaled by at a text size (bitmap faces
		// with IntegerScale snap it to whole units per font pixel).
		float GetRenderSize(float size) const;

		FontStyleMetrics GetStyleMetrics(float size) const;

		// A glyph with the same advance (an obfuscated character), picked by `random` among
		// the face's printable ASCII glyphs; the glyph itself when none matches.
		std::uint32_t GetSubstituteGlyph(std::uint32_t glyph, std::uint32_t random) const;

		struct BitmapFace;

	  private:

		friend class GlyphAtlas;
		struct Impl;

		struct GlyphBitmap
		{
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			float Left = 0.0f;
			float Top = 0.0f;
			std::vector<std::uint8_t> Pixels; // Top-down RGB, linear distance values (bitmap faces: RGBA, straight sRGB).
		};

		GlyphBitmap Rasterize(std::uint32_t glyph, float emSize, float range, std::uint32_t maxDimension) const;

		// Bitmap faces: the glyph's texels; Left/Top in em units (EmPixels), Width/Height texels.
		GlyphBitmap RasterizeBitmap(std::uint32_t glyph, float& emWidth, float& emHeight, std::uint32_t maxDimension) const;

		std::unique_ptr<Impl> impl;
		std::unique_ptr<BitmapFace> bitmap;

	};

} // namespace Swim::Text
