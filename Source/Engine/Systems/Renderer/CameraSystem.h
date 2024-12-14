#pragma once

namespace Engine
{

  struct Camera
  {
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f };
    float fov = 45.0f;
    float aspect = 1.0f;
    float nearClip = 0.1f;
    float farClip = 100.0f;
  };

  class CameraSystem : public Machine
  {

  public:

    CameraSystem();

    void Update(double dt) override;

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const;

    Camera camera;

  };

}
