#pragma once

#include "Engine/Systems/Text/FontFace.h"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace Swim::Text
{

	// The bitmap backend of a FontFace: grid blocks and explicit glyphs, resolved lazily per
	// 256-code-point block. Resolved blocks are published through atomics, so lookups after
	// the first are lock-free; loading is serialized by Mutex.
	struct FontFace::BitmapFace
	{
		static constexpr std::uint32_t BlockCount = 0x110000u >> 8;

		struct Page
		{
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			std::vector<std::uint8_t> Rgba;
			float FontPixelsPerTexel = 1.0f;
		};

		struct Glyph
		{
			const Page* Source = nullptr; // Null for glyphs without texels (spaces).
			std::uint32_t X = 0;
			std::uint32_t Y = 0;
			std::uint32_t Width = 0; // Trimmed to the last opaque column.
			std::uint32_t Height = 0;
			float Advance = 0.0f;  // Font pixels.
			float Top = 0.0f;	   // Font pixels from the rectangle's top down to the baseline.
			float TexelScale = 1.0f; // Font pixels per texel.
			bool Exists = false;
		};

		struct Block
		{
			std::array<Glyph, 256> Glyphs;
		};

		explicit BitmapFace(BitmapFontDesc desc);

		const Glyph* Find(char32_t codePoint) const; // Null when the face has no glyph for it.

		float PixelUnits(float size) const; // Logical units per font pixel at a text size.

		BitmapFontDesc Desc;
		std::unordered_map<std::uint32_t, std::vector<std::pair<char32_t, BitmapFontGlyph>>> ExplicitByBlock;

	  private:

		const Block* Load(std::uint32_t block) const;

		const Page* LoadPage(std::uint32_t id) const; // Requires Mutex.

		mutable std::mutex Mutex;
		mutable std::unordered_map<std::uint32_t, std::unique_ptr<Page>> Pages; // Null: absent.
		mutable std::vector<std::unique_ptr<Block>> Store;
		mutable std::unique_ptr<std::atomic<const Block*>[]> Blocks; // Null: not loaded yet.
	};

	// One horizontal run of a bitmap face: one glyph per code point (no kerning or
	// ligatures), visual order (reversed for right-to-left).
	ShapedRun ShapeBitmap(const FontFace::BitmapFace& face, std::string_view utf8, float size, const ShapeOptions& options);

} // namespace Swim::Text
