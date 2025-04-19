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
    camera.SetClipPlanes(0.1f, 100.0f);

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

  const glm::mat4& CameraSystem::GetViewMatrix() const
  {
    return camera.GetViewMatrix();
  }

  const glm::mat4& CameraSystem::GetProjectionMatrix() const
  {
    return camera.GetProjectionMatrix();
  }

}
