#include "Engine/Systems/Text/Utf8.h"

namespace Swim::Text
{
	Utf8Decoded DecodeUtf8(std::string_view text, std::size_t offset)
	{
		const auto byte = [&](std::size_t i)
		{
			return static_cast<std::uint8_t>(text[i]);
		};
		const std::uint8_t lead = byte(offset);
		if (lead < 0x80)
		{
			return { lead, 1, true };
		}
		std::uint32_t length = 0;
		char32_t value = 0;
		char32_t minimum = 0;
		if ((lead & 0xE0) == 0xC0)
		{
			length = 2;
			value = lead & 0x1F;
			minimum = 0x80;
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			length = 3;
			value = lead & 0x0F;
			minimum = 0x800;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			length = 4;
			value = lead & 0x07;
			minimum = 0x10000;
		}
		else
		{
			return {};
		}
		if (offset + length > text.size())
		{
			return {};
		}
		for (std::uint32_t i = 1; i < length; ++i)
		{
			const std::uint8_t continuation = byte(offset + i);
			if ((continuation & 0xC0) != 0x80)
			{
				return {};
			}
			value = (value << 6) | (continuation & 0x3F);
		}
		if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
		{
			return {};
		}
		return { value, length, true };
	}

	std::size_t PreviousUtf8(std::string_view text, std::size_t offset)
	{
		if (offset == 0)
		{
			return 0;
		}
		std::size_t start = offset - 1;
		while (start > 0 && offset - start < 4 && (static_cast<std::uint8_t>(text[start]) & 0xC0) == 0x80)
		{
			--start;
		}
		const auto decoded = DecodeUtf8(text, start);
		return decoded.Valid && start + decoded.Length == offset ? start : offset - 1;
	}

	bool IsValidUtf8(std::string_view text)
	{
		for (std::size_t offset = 0; offset < text.size();)
		{
			const auto decoded = DecodeUtf8(text, offset);
			if (!decoded.Valid)
			{
				return false;
			}
			offset += decoded.Length;
		}
		return true;
	}

	void AppendUtf8(std::string& out, char32_t codePoint)
	{
		if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
		{
			codePoint = ReplacementCharacter;
		}
		if (codePoint < 0x80)
		{
			out.push_back(static_cast<char>(codePoint));
		}
		else if (codePoint < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
			out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else if (codePoint < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
			out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
	}

	std::string SanitizeUtf8(std::string_view text)
	{
		if (IsValidUtf8(text))
		{
			return std::string(text);
		}
		std::string result;
		result.reserve(text.size() + 8);
		for (std::size_t offset = 0; offset < text.size();)
		{
			const auto decoded = DecodeUtf8(text, offset);
			if (decoded.Valid)
			{
				result.append(text.substr(offset, decoded.Length));
			}
			else
			{
				AppendUtf8(result, ReplacementCharacter);
			}
			offset += decoded.Length;
		}
		return result;
	}
} // namespace Swim::Text
