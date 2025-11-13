#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>

namespace Engine
{

	enum class EngineState : std::uint8_t; // fwd decl for signatures below

	inline constexpr EngineState operator|(EngineState a, EngineState b)
	{
		using U = std::underlying_type_t<EngineState>;
		return static_cast<EngineState>(static_cast<U>(a) | static_cast<U>(b));
	}

	inline constexpr EngineState operator&(EngineState a, EngineState b)
	{
		using U = std::underlying_type_t<EngineState>;
		return static_cast<EngineState>(static_cast<U>(a) & static_cast<U>(b));
	}

	inline constexpr EngineState operator^(EngineState a, EngineState b)
	{
		using U = std::underlying_type_t<EngineState>;
		return static_cast<EngineState>(static_cast<U>(a) ^ static_cast<U>(b));
	}

	inline constexpr EngineState operator~(EngineState a)
	{
		using U = std::underlying_type_t<EngineState>;
		return static_cast<EngineState>(~static_cast<U>(a));
	}

	inline EngineState& operator|=(EngineState& a, EngineState b)
	{
		a = (a | b);
		return a;
	}

	inline EngineState& operator&=(EngineState& a, EngineState b)
	{
		a = (a & b);
		return a;
	}

	inline EngineState& operator^=(EngineState& a, EngineState b)
	{
		a = (a ^ b);
		return a;
	}

	// actual definition here
	enum class EngineState : std::uint8_t
	{
		None = 0,
		Playing = 1u << 0, // 0b0001
		Paused = 1u << 1, // 0b0010
		Editing = 1u << 2, // 0b0100
		Stopped = 1u << 3, // 0b1000

		All = (Playing | Paused | Editing | Stopped)
	};

	inline constexpr bool HasAny(EngineState mask, EngineState flags)
	{
		return (mask & flags) != EngineState::None;
	}

	inline constexpr bool HasAll(EngineState mask, EngineState flags)
	{
		return (mask & flags) == flags;
	}

	// Helpers to parse --state into EngineState bitflags:

	inline EngineState ParseEngineStateToken(std::string token)
	{
		// trim spaces
		token.erase(token.begin(), std::find_if(token.begin(), token.end(),
			[](unsigned char ch) { return !std::isspace(ch); }));
		token.erase(std::find_if(token.rbegin(), token.rend(),
			[](unsigned char ch) { return !std::isspace(ch); }).base(), token.end());

		// lowercase
		std::transform(token.begin(), token.end(), token.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (token == "playing") return EngineState::Playing;
		if (token == "paused")  return EngineState::Paused;
		if (token == "editing") return EngineState::Editing;
		if (token == "stopped") return EngineState::Stopped;
		if (token == "all")     return EngineState::All;
		if (token == "none")    return EngineState::None;

		// Try numeric (dec or hex)
		char* end = nullptr;
		unsigned long long v = std::strtoull(token.c_str(), &end, 0);
		if (end != token.c_str() && *end == '\0')
		{
			using U = std::underlying_type_t<EngineState>;
			return static_cast<EngineState>(static_cast<U>(v));
		}

		// Unknown -> return None; caller can OR multiple tokens and default if zero.
		return EngineState::None;
	}

	inline EngineState ParseEngineStateArg(const std::string& value)
	{
		// Support delimiters: comma or pipe
		EngineState result = EngineState::None;

		std::size_t start = 0;
		while (start <= value.size())
		{
			std::size_t comma = value.find_first_of(",|", start);
			std::string token = (comma == std::string::npos)
				? value.substr(start)
				: value.substr(start, comma - start);

			result |= ParseEngineStateToken(token);

			if (comma == std::string::npos) break;
			start = comma + 1;
		}

		return result;
	}

}
