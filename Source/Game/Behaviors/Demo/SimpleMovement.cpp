#include "PCH.h"
#include "SimpleMovement.h"
#include "Engine/Components/Transform.h"

namespace Game
{

	void SimpleMovement::Update(double dt)
	{
		const float entityMoveSpeed = 5.0f;
		glm::vec3 entityMoveDir{ 0.0f };

		// Arrow keys + Z/X for entity movement.
		if (input->IsKeyDown(VK_UP))
		{
			entityMoveDir += glm::vec3(0.0f, 0.0f, -1.0f); // Forward
		}
		if (input->IsKeyDown(VK_DOWN))
		{
			entityMoveDir += glm::vec3(0.0f, 0.0f, 1.0f); // Backward
		}
		if (input->IsKeyDown(VK_LEFT))
		{
			entityMoveDir += glm::vec3(-1.0f, 0.0f, 0.0f); // Left
		}
		if (input->IsKeyDown(VK_RIGHT))
		{
			entityMoveDir += glm::vec3(1.0f, 0.0f, 0.0f); // Right
		}
		if (input->IsKeyDown(VK_PRIOR)) // page up
		{
			entityMoveDir += glm::vec3(0.0f, 1.0f, 0.0f); // Up
		}
		if (input->IsKeyDown(VK_NEXT)) // page down
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