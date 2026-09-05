#include "PCH.h"
#include "SimpleMovement.h"
#include "Engine/Components/Transform.h"
#include "Engine/Input/InputSystem.h"

namespace Game
{

	void SimpleMovement::Update(double dt)
	{
		const float entityMoveSpeed = 5.0f;
		glm::vec3 entityMoveDir{ 0.0f };

		// Arrow keys + Z/X for entity movement.
		if (input->IsKeyDown(Swim::Platform::KeyCode::Up))
		{
			entityMoveDir += glm::vec3(0.0f, 0.0f, -1.0f); // Forward
		}
		if (input->IsKeyDown(Swim::Platform::KeyCode::Down))
		{
			entityMoveDir += glm::vec3(0.0f, 0.0f, 1.0f); // Backward
		}
		if (input->IsKeyDown(Swim::Platform::KeyCode::Left))
		{
			entityMoveDir += glm::vec3(-1.0f, 0.0f, 0.0f); // Left
		}
		if (input->IsKeyDown(Swim::Platform::KeyCode::Right))
		{
			entityMoveDir += glm::vec3(1.0f, 0.0f, 0.0f); // Right
		}
		if (input->IsKeyDown(Swim::Platform::KeyCode::PageUp)) // page up
		{
			entityMoveDir += glm::vec3(0.0f, 1.0f, 0.0f); // Up
		}
		if (input->IsKeyDown(Swim::Platform::KeyCode::PageDown)) // page down
		{
			entityMoveDir += glm::vec3(0.0f, -1.0f, 0.0f); // Down
		}

		// Apply entity movement if any
		if (glm::length(entityMoveDir) > 0.0f)
		{
			entityMoveDir = glm::normalize(entityMoveDir) * entityMoveSpeed * static_cast<float>(dt);
			transform->GetPositionRef() += entityMoveDir;
		}
	}

}