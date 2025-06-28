#include "PCH.h"
#include "CameraSystem.h"

namespace Engine
{

  CameraSystem::CameraSystem() : camera{} {}

  int CameraSystem::Init()
  {
    RefreshAspect();

    camera.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.SetFOV(45.0f);
    camera.SetClipPlanes(0.1f, 1000.0f); // the 1000 is basically the render distance

    return 0;
  }

  void CameraSystem::RefreshAspect()
  {
    auto instance = SwimEngine::GetInstance();
    float aspect = static_cast<float>(instance->GetWindowWidth()) / static_cast<float>(instance->GetWindowHeight());
    camera.SetAspect(aspect);
  }

  void CameraSystem::Update(double dt)
  {
    // nop for now
  }

  glm::vec2 CameraSystem::ScreenToWorld(const glm::vec2& screenPos, const glm::vec2& windowSize) const
  {
    // Convert screen space to normalized device coordinates [-1, 1]
    glm::vec2 ndc;
    ndc.x = (2.0f * screenPos.x) / windowSize.x - 1.0f;
    ndc.y = 1.0f - (2.0f * screenPos.y) / windowSize.y;

    // Transform to world space
    glm::vec4 clipCoords = glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    glm::mat4 invViewProj = glm::inverse(GetProjectionMatrix() * GetViewMatrix());
    glm::vec4 worldCoords = invViewProj * clipCoords;

    return glm::vec2(worldCoords.x, worldCoords.y);
  }

}
