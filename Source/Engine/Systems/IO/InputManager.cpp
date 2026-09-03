#include "PCH.h"
#include "InputManager.h"

namespace Engine
{

	int InputManager::Awake()
	{
		inputSystem.Reset();
		return 0;
	}

	int InputManager::Init()
	{
		return 0;
	}

	void InputManager::Update(double dt)
	{
		(void)dt;
		inputSystem.AdvanceFrame();
	}

	glm::vec2 InputManager::GetMousePosition() const
	{
		const Swim::Platform::Float2 position = inputSystem.GetMousePosition();
		return { position.X, position.Y };
	}

	glm::vec2 InputManager::GetMousePositionDelta() const
	{
		const Swim::Platform::Float2 delta = inputSystem.GetMousePositionDelta();
		return { delta.X, delta.Y };
	}

}
