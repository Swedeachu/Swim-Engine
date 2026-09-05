#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace Engine
{

	struct SerializedEntityId
	{
		std::uint64_t Value = 0;

		constexpr bool IsValid() const { return Value != 0; }
		explicit constexpr operator bool() const { return IsValid(); }

		auto operator<=>(const SerializedEntityId&) const = default;
	};

}

namespace std
{

	template<>
	struct hash<Engine::SerializedEntityId>
	{
		std::size_t operator()(const Engine::SerializedEntityId& value) const noexcept
		{
			return std::hash<std::uint64_t>{}(value.Value);
		}
	};

}
