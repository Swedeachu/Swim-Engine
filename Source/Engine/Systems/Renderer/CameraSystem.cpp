#include "PCH.h"
#include "CameraSystem.h"
#include "Library/glm/gtc/matrix_transform.hpp"
#include "Engine/SwimEngine.h"

namespace Engine
{

	CameraSystem::CameraSystem() : camera{} {}

	int CameraSystem::Init()
	{
		camera.position = glm::vec3(0.0f, 0.0f, 0.0f);
		camera.fov = 45.0f;

		auto instance = SwimEngine::GetInstance();

		camera.aspect = (float)instance->GetWindowWidth() / (float)instance->GetWindowHeight();
		camera.nearClip = 0.1f;
		camera.farClip = 100.0f;

		return 0;
	}

	void CameraSystem::Update(double dt)
	{
		// Compute the view matrix
		glm::mat4 view = glm::lookAt(
			camera.position,
			camera.position + glm::vec3(0.0f, 0.0f, -1.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);

		// Compute the projection matrix
		glm::mat4 proj = glm::perspective(glm::radians(camera.fov), camera.aspect, camera.nearClip, camera.farClip);
		proj[1][1] *= -1; // Flip Y for Vulkan
	}

	// TODO: setting camera pitch and yaw and roll
	glm::mat4 CameraSystem::GetViewMatrix() const
	{
		return glm::lookAt
		(
			camera.position,
			camera.position + glm::vec3(0.0f, 0.0f, -1.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);
	}

	glm::mat4 CameraSystem::GetProjectionMatrix() const
	{
		glm::mat4 proj = glm::perspective(glm::radians(camera.fov), camera.aspect, camera.nearClip, camera.farClip);
		proj[1][1] *= -1; // Flip Y for Vulkan
		return proj;
	}

}
