#include "PCH.h"
#include "Spin.h"
#include "Engine/Components/Transform.h"

namespace Game
{

	void Spin::Update(double dt)
	{
		if (!transform)
		{
			return;
		}

		accumulatedAngle += spinSpeed * static_cast<float>(dt);

		// Wrap around to prevent precision issues
		if (accumulatedAngle > 360.0f)
		{
			accumulatedAngle -= 360.0f;
		}

		glm::quat rot = glm::angleAxis(glm::radians(accumulatedAngle), glm::vec3(0.0f, 1.0f, 0.0f));
		transform->SetRotation(rot);
	}

}
