#pragma once
#include <cstdint>
#include <type_traits>

namespace Engine
{

  // Bitflag states for the engine. Behaviors and systems will use this for determining when they should run based on SwimEngine::EngineState
  enum class EngineState : std::uint8_t
  {
    None = 0,
    Playing = 1 << 0, // 0b0001
    Paused = 1 << 1, // 0b0010
    Editing = 1 << 2, // 0b0100
    Stopped = 1 << 3, // 0b1000
    All = Playing | Paused | Editing | Stopped
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

  inline constexpr bool HasAny(EngineState mask, EngineState flags)
  {
    return static_cast<bool>(mask & flags);
  }

  inline constexpr bool HasAll(EngineState mask, EngineState flags)
  {
    return (mask & flags) == flags;
  }

}
