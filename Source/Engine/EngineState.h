#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

namespace Engine
{
	// Runtime simulation state. The engine is in exactly one of Playing, Paused or
	// Stopped (EngineStateMachine); the enum is also a bit mask so behaviours and
	// systems can declare which states they run in (BehaviorComponents).
	//
	//   Playing  the simulation advances (physics, animation, particles, behaviours)
	//   Paused   the simulation is frozen; single steps may still be requested
	//   Stopped  nothing simulates; Play starts again (scenes may reset on Play)
	//
	// The editor-era Editing state was removed with the editor (Phase 22).
	enum class EngineState : std::uint8_t
	{
		None = 0,
		Playing = 1u << 0,
		Paused = 1u << 1,
		Stopped = 1u << 3, // Bit 2 was Editing; kept free so old masks stay recognisable.

		All = Playing | Paused | Stopped
	};

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
		return static_cast<EngineState>(~static_cast<U>(a) & static_cast<U>(EngineState::All));
	}

	inline EngineState& operator|=(EngineState& a, EngineState b)
	{
		a = a | b;
		return a;
	}

	inline EngineState& operator&=(EngineState& a, EngineState b)
	{
		a = a & b;
		return a;
	}

	inline EngineState& operator^=(EngineState& a, EngineState b)
	{
		a = a ^ b;
		return a;
	}

	inline constexpr bool HasAnyEngineStates(EngineState mask, EngineState flags)
	{
		return (mask & flags) != EngineState::None;
	}

	inline constexpr bool HasAllEngineStates(EngineState mask, EngineState flags)
	{
		return (mask & flags) == flags;
	}

	// True for exactly one of Playing, Paused, Stopped.
	inline constexpr bool IsSingleEngineState(EngineState state)
	{
		return state == EngineState::Playing || state == EngineState::Paused || state == EngineState::Stopped;
	}

	inline std::string_view ToString(EngineState state)
	{
		switch (state)
		{
		case EngineState::None:
			return "None";
		case EngineState::Playing:
			return "Playing";
		case EngineState::Paused:
			return "Paused";
		case EngineState::Stopped:
			return "Stopped";
		case EngineState::All:
			return "All";
		default:
			return "Mask";
		}
	}

	// One token of --state / a mask string: playing, paused, stopped, all, none or a
	// number. Unknown tokens return None (callers treat that as an error unless the
	// token was "none").
	inline EngineState ParseEngineStateToken(std::string token)
	{
		token.erase(token.begin(),
			std::find_if(token.begin(), token.end(),
				[](unsigned char ch)
				{
					return !std::isspace(ch);
				}));
		token.erase(std::find_if(token.rbegin(), token.rend(),
						[](unsigned char ch)
						{
							return !std::isspace(ch);
						})
						.base(),
			token.end());
		std::transform(token.begin(), token.end(), token.begin(),
			[](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});

		if (token == "playing" || token == "play")
		{
			return EngineState::Playing;
		}
		if (token == "paused" || token == "pause")
		{
			return EngineState::Paused;
		}
		if (token == "stopped" || token == "stop")
		{
			return EngineState::Stopped;
		}
		if (token == "all")
		{
			return EngineState::All;
		}
		if (token == "none")
		{
			return EngineState::None;
		}

		char* end = nullptr;
		const unsigned long long v = std::strtoull(token.c_str(), &end, 0);
		if (end != token.c_str() && *end == '\0')
		{
			using U = std::underlying_type_t<EngineState>;
			return static_cast<EngineState>(static_cast<U>(v) & static_cast<U>(EngineState::All));
		}
		return EngineState::None;
	}
} // namespace Engine
