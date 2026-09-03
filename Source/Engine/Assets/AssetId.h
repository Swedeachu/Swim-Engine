#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace Swim::Assets
{

	struct AssetId
	{
		std::uint64_t Value = 0;

		constexpr bool IsValid() const { return Value != 0; }
		explicit constexpr operator bool() const { return IsValid(); }

		auto operator<=>(const AssetId&) const = default;
	};

}

namespace std
{

	template<>
	struct hash<Swim::Assets::AssetId>
	{
		std::size_t operator()(const Swim::Assets::AssetId& value) const noexcept
		{
			return std::hash<std::uint64_t>{}(value.Value);
		}
	};

}
