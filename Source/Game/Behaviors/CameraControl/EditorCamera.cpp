#include "PCH.h"
#include "EditorCamera.h"

namespace Game
{

	void EditorCamera::Update(double dt)
	{
		// Movement speed constants
		const float cameraMoveSpeed = 5.0f;
		const float boostMultiplier = 3.0f;
		const float mouseSensitivity = 0.1f;

		// Get the active camera
		auto& camera = cameraSystem->GetCamera();
		glm::vec3 position = camera.GetPosition();

		// ----- Mouse Look -----
		if (input->IsKeyDown(VK_RBUTTON))
		{
			glm::vec2 mouseDelta = input->GetMousePositionDelta();

			// Update yaw (horizontal) and pitch (vertical)
			yaw += mouseDelta.x * mouseSensitivity;
			pitch -= mouseDelta.y * mouseSensitivity;

			// Clamp pitch to prevent flipping
			pitch = glm::clamp(pitch, -89.9f, 89.9f);

			// Yaw is around global Y
			glm::quat qYaw = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));

			// Pitch is around camera's local X
			glm::quat qPitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));

			// Combined rotation: yaw first, then pitch
			glm::quat rotation = qYaw * qPitch;

			camera.SetRotation(rotation);
		}

		glm::quat rotation = camera.GetRotation(); // Get updated rotation

		// ----- Movement -----
		glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
		glm::vec3 forward = glm::normalize(rotationMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
		glm::vec3 right = glm::normalize(rotationMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f); // World up

		glm::vec3 movement(0.0f);

		if (input->IsKeyDown('W'))
		{
			movement += forward;
		}
		if (input->IsKeyDown('S'))
		{
			movement -= forward;
		}
		if (input->IsKeyDown('A'))
		{
			movement -= right;
		}
		if (input->IsKeyDown('D'))
		{
			movement += right;
		}
		if (input->IsKeyDown(VK_SPACE))
		{
			movement += up;
		}
		if (input->IsKeyDown(VK_SHIFT))
		{
			movement -= up;
		}

		if (glm::length(movement) > 0.0f)
		{
			movement = glm::normalize(movement);
		}

		float speed = cameraMoveSpeed;
		if (input->IsKeyDown(VK_CONTROL))
		{
			speed *= boostMultiplier;
		}

		position += movement * speed * static_cast<float>(dt);
		camera.SetPosition(position);
	}

}
