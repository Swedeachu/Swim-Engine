#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Swim::Assets
{

	struct ContentHash
	{
		std::array<std::uint8_t, 32> Bytes{};

		bool IsZero() const;
		std::string ToHex() const;
		static ContentHash FromHex(std::string_view text);

		auto operator<=>(const ContentHash&) const = default;
	};

	ContentHash ComputeContentHash(std::span<const std::byte> bytes);
	ContentHash ComputeContentHash(std::string_view text);

}

namespace std
{

	template<>
	struct hash<Swim::Assets::ContentHash>
	{
		std::size_t operator()(const Swim::Assets::ContentHash& value) const noexcept
		{
			std::size_t result = 0xcbf29ce484222325ull;
			for (std::uint8_t byte : value.Bytes)
			{
				result ^= static_cast<std::size_t>(byte);
				result *= 0x100000001b3ull;
			}
			return result;
		}
	};

}
