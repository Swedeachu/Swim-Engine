#pragma once

#include "Engine/Systems/Text/FontFace.h"

#include <functional>
#include <limits>
#include <span>
#include <unordered_map>

namespace Swim::Text
{
	inline constexpr std::uint32_t NoAtlasPage = std::numeric_limits<std::uint32_t>::max();

	struct GlyphAtlasDesc
	{
		std::uint32_t PageSize = 512;
		std::uint32_t MaxPages = 8;
		float EmSize = 48.0f;
		float DistanceRange = 4.0f; // Full signed distance range in atlas texels.
	};

	struct AtlasGlyph
	{
		std::uint32_t Page = NoAtlasPage;
		std::uint32_t X = 0;
		std::uint32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		float Left = 0.0f; // Padded quad bounds in em units, baseline origin, positive up.
		float Top = 0.0f;
		float WidthEm = 0.0f;
		float HeightEm = 0.0f;
	};

	// A band of whole page rows, [Y, Y + Height). Empty when Height is 0.
	struct AtlasRowSpan
	{
		std::uint32_t Y = 0;
		std::uint32_t Height = 0;

		bool operator==(const AtlasRowSpan&) const = default;
	};

	struct AtlasPageView
	{
		std::uint32_t Size = 0;
		std::uint64_t Revision = 0;
		std::span<const std::uint8_t> Pixels; // Top-down RGB8 UNORM, never sRGB.
	};

	// Single-owner cache; call on the owning thread or externally synchronize.
	// Entries/pages never move or evict logically, so retained paint UVs stay valid.
	// GPU upload and timeline retirement belong to the consumer, not this cache.
	class GlyphAtlas
	{
	  public:
		explicit GlyphAtlas(const GlyphAtlasDesc& desc = {});
		GlyphAtlas(const GlyphAtlas&) = delete;
		GlyphAtlas& operator=(const GlyphAtlas&) = delete;

		// Keeps the face alive; throws length_error when the fixed budget is full.
		// Empty outlines have NoAtlasPage and consume no space. Returns by value.
		AtlasGlyph Get(const std::shared_ptr<const FontFace>& face, std::uint32_t glyph);

		// Runs body(i) for i in [0, count), possibly concurrently (for example through
		// JobSystem::ParallelFor); returns after every call finished.
		using ParallelFor = std::function<void(std::size_t count, const std::function<void(std::size_t)>& body)>;
		// Populates the atlas before a time-critical frame. The distance fields of the
		// missing glyphs are generated through parallelFor (serially without one), then
		// packed on the calling thread in the given order, so the result equals calling
		// Get for each glyph in order. If any generation fails nothing is inserted and
		// the first exception is rethrown; a full budget throws like Get, keeping the
		// glyphs packed before it. Returns the number of glyphs added.
		std::size_t Prewarm(
			const std::shared_ptr<const FontFace>& face, std::span<const std::uint32_t> glyphs, const ParallelFor& parallelFor = {});
		bool Contains(const FontFace& face, std::uint32_t glyph) const;
		AtlasPageView GetPage(std::uint32_t page) const;
		// The rows written after the page had `sinceRevision` (a consumer's last
		// uploaded revision): the union of every later glyph's rows. Everything outside
		// is unchanged since then. Throws std::out_of_range for an unknown page or a
		// revision the page has not reached.
		AtlasRowSpan GetChangedRows(std::uint32_t page, std::uint64_t sinceRevision) const;

		std::uint32_t GetPageCount() const { return static_cast<std::uint32_t>(pages.size()); }

		std::size_t GetGlyphCount() const { return glyphs.size(); }

		const GlyphAtlasDesc& GetDesc() const { return desc; }

	  private:
		struct Key
		{
			const FontFace* Face = nullptr;
			std::uint32_t Glyph = 0;
			bool operator==(const Key&) const = default;
		};

		struct KeyHash
		{
			std::size_t operator()(const Key& key) const;
		};

		struct Entry
		{
			std::shared_ptr<const FontFace> Face;
			AtlasGlyph Glyph;
		};

		AtlasGlyph Insert(const std::shared_ptr<const FontFace>& face, std::uint32_t glyph, const FontFace::GlyphBitmap& bitmap);

		struct Page
		{
			std::vector<std::uint8_t> Pixels;
			std::uint32_t X = 1;
			std::uint32_t Y = 1;
			std::uint32_t RowHeight = 0;
			std::uint64_t Revision = 0;		  // Glyphs written so far.
			std::vector<AtlasRowSpan> Writes; // Rows of the glyph written at revision i + 1.
		};

		GlyphAtlasDesc desc;
		std::vector<Page> pages;
		std::unordered_map<Key, Entry, KeyHash> glyphs;
	};
} // namespace Swim::Text
