#pragma once

#include "Library/glm/glm.hpp"
#include <array>

namespace Engine
{

  // Central color list
#define DEBUG_COLOR_LIST \
    X(Red,        glm::vec3(1.0f, 0.0f, 0.0f)) \
    X(Green,      glm::vec3(0.0f, 1.0f, 0.0f)) \
    X(Blue,       glm::vec3(0.0f, 0.0f, 1.0f)) \
    X(Cyan,       glm::vec3(0.0f, 1.0f, 1.0f)) \
    X(Magenta,    glm::vec3(1.0f, 0.0f, 1.0f)) \
    X(Yellow,     glm::vec3(1.0f, 1.0f, 0.0f)) \
    X(Black,      glm::vec3(0.0f, 0.0f, 0.0f)) \
    X(White,      glm::vec3(1.0f, 1.0f, 1.0f)) \
    X(Gray,       glm::vec3(0.5f, 0.5f, 0.5f)) \
    X(DarkGray,   glm::vec3(0.25f, 0.25f, 0.25f)) \
    X(LightGray,  glm::vec3(0.75f, 0.75f, 0.75f)) \
    X(Orange,     glm::vec3(1.0f, 0.5f, 0.0f)) \
    X(Pink,       glm::vec3(1.0f, 0.4f, 0.7f)) \
    X(Purple,     glm::vec3(0.5f, 0.0f, 0.5f)) \
    X(Lime,       glm::vec3(0.7f, 1.0f, 0.0f)) \
    X(Brown,      glm::vec3(0.6f, 0.3f, 0.0f)) \
    X(Turquoise,  glm::vec3(0.25f, 0.88f, 0.82f)) \
    X(SkyBlue,    glm::vec3(0.53f, 0.81f, 0.92f)) \
    X(Gold,       glm::vec3(1.0f, 0.84f, 0.0f)) \
    X(Silver,     glm::vec3(0.75f, 0.75f, 0.75f)) \
    X(Mint,       glm::vec3(0.6f, 1.0f, 0.6f)) \
    X(Navy,       glm::vec3(0.0f, 0.0f, 0.5f)) \
    X(Beige,      glm::vec3(0.96f, 0.96f, 0.86f))

  // Generate enum
  enum class DebugColor
  {
  #define X(name, value) name,
    DEBUG_COLOR_LIST
  #undef X
    Count // always last for safe iteration
  };

  // Generate constexpr array of values
  inline constexpr std::array<glm::vec3, static_cast<size_t>(DebugColor::Count)> DebugColorValues = { {
      #define X(name, value) value,
      DEBUG_COLOR_LIST
      #undef X
  } };

  // Accessor function
  inline constexpr glm::vec3 GetDebugColorValue(DebugColor color)
  {
    return DebugColorValues[static_cast<size_t>(color)];
  }

} // namespace Engine
