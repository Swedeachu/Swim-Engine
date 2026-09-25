#pragma once

#include "Engine/Systems/Text/FontFace.h"

#include <memory>
#include <span>
#include <vector>

namespace Swim::Text
{
	// An ordered fallback chain: face 0 is the primary face. Immutable and safe to
	// share between threads and UI documents. There is no OS font lookup; the
	// application decides which faces (and in which order) may render its text.
	class FontCollection final
	{
	  public:
		static constexpr std::size_t MaxFaces = 64;
		static constexpr std::uint32_t NoPreference = 0xFFFFFFFFu;

		// Throws std::invalid_argument for an empty chain, a null face or more than MaxFaces.
		explicit FontCollection(std::vector<std::shared_ptr<const FontFace>> faces);
		static std::shared_ptr<const FontCollection> Single(std::shared_ptr<const FontFace> face);

		std::uint32_t GetCount() const { return static_cast<std::uint32_t>(faces.size()); }

		const std::shared_ptr<const FontFace>& GetFace(std::uint32_t index) const { return faces.at(index); }

		const FontFace& GetPrimary() const { return *faces.front(); }

		// Cluster-aware fallback: the face that renders every code point of one grapheme
		// cluster, ignoring default-ignorable and control code points (ZWJ, variation
		// selectors, bidi controls, ...). `preferred` (usually the previous cluster's
		// face) wins for whitespace, punctuation and symbols it covers, so a run is not
		// split around a space. Otherwise the first covering face in chain order; the
		// primary face (0) when none covers the cluster (it then shapes .notdef).
		std::uint32_t SelectFace(std::span<const char32_t> cluster, std::uint32_t preferred = NoPreference) const;
		bool Covers(std::uint32_t face, std::span<const char32_t> cluster) const;

		// True for code points that never need a glyph of their own.
		static bool IsIgnorable(char32_t codePoint);
		// Whitespace, punctuation and symbols (Unicode general categories Z*, P*, S*).
		static bool IsNeutral(char32_t codePoint);

	  private:
		std::vector<std::shared_ptr<const FontFace>> faces;
	};
} // namespace Swim::Text
