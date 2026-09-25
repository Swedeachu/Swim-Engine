#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Swim::Text
{
	inline constexpr char32_t ReplacementCharacter = U'�';

	// One decoded code point and the number of bytes it occupied. Invalid or
	// truncated sequences, overlong forms, surrogates and values above U+10FFFF
	// decode as U+FFFD consuming one byte (the WHATWG/Unicode "maximal subpart"
	// policy reduced to one byte, so every byte is visited exactly once).
	struct Utf8Decoded
	{
		char32_t CodePoint = ReplacementCharacter;
		std::uint32_t Length = 1;
		bool Valid = false;
	};

	// Decodes the sequence starting at `offset` (< text.size()).
	Utf8Decoded DecodeUtf8(std::string_view text, std::size_t offset);
	// The start of the code point that ends before `offset` (> 0) in valid UTF-8.
	std::size_t PreviousUtf8(std::string_view text, std::size_t offset);
	bool IsValidUtf8(std::string_view text);
	// Returns valid UTF-8: every invalid byte becomes U+FFFD (three bytes). Text
	// layout and editing work on sanitized text so that every layer (bidi,
	// breaking, shaping, carets) agrees on byte offsets.
	std::string SanitizeUtf8(std::string_view text);
	void AppendUtf8(std::string& out, char32_t codePoint);
} // namespace Swim::Text
