#pragma once

#include "Library/EnTT/entt.hpp"
#include "Library/glm/glm.hpp"
#include "Engine//Utility/ColorConstants.h"

namespace Engine
{

  // forward declare
  class Mesh;

  struct DebugWireBoxData
  {
    DebugColor color;
  };

  class SceneDebugDraw
  {

  public:

    SceneDebugDraw() = default;

    void Init();
    void Clear();

    void SetEnabled(bool value) { enabled = value; }
    const bool IsEnabled() const { return enabled; }

    // Submits box using min/max AABB corners + preset color
    void SubmitWireframeBoxAABB
    (
      const glm::vec3& min,
      const glm::vec3& max,
      DebugColor color = DebugColor::Red
    );

    // Submits box using position, scale, rotation + preset color
    void SubmitWireframeBox
    (
      const glm::vec3& position,
      const glm::vec3& scale,
      float pitchDegrees = 0.0f,
      float yawDegrees = 0.0f,
      float rollDegrees = 0.0f,
      DebugColor color = DebugColor::Red
    );

    entt::registry& GetRegistry()
    {
      return debugRegistry;
    }

    const std::shared_ptr<Mesh>& GetWireframeCubeMesh(DebugColor color) const
    {
      static constexpr std::array<const std::shared_ptr<Mesh> SceneDebugDraw::*, static_cast<size_t>(DebugColor::Count)> meshPointers = { {
          #define X(NAME, VALUE) &SceneDebugDraw::wireframeCubeMesh##NAME,
          DEBUG_COLOR_LIST
          #undef X
      } };

      return this->*meshPointers[static_cast<size_t>(color)];
    }

  private:

    std::shared_ptr<Mesh> CreateAndRegisterWireframeBoxMesh(DebugColor color, std::string meshName);

    bool enabled{ false };
    entt::registry debugRegistry;

    // Mesh fields generated from enum
  #define X(NAME, VALUE) std::shared_ptr<Mesh> wireframeCubeMesh##NAME = nullptr;
    DEBUG_COLOR_LIST
    #undef X
  };

}
