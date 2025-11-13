#pragma once

#include "Engine/Systems/Entity/Behavior.h"

namespace Engine
{

	class DragUiBehavior : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		int Init() override;

		void OnLeftClickUp() override;
		void OnLeftClickDown() override;
		void Update(double dt) override;

	private:

		bool isDragging{ false };
		bool clampedInsideWindow{ true };

		glm::vec2 dragStartMouse{ 0.0f, 0.0f };
		glm::vec3 dragStartPos{ 0.0f, 0.0f, 0.0f };
		glm::vec2 grabOffset{ 0.0f, 0.0f };

	};

}
