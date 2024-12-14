#include "PCH.h"
#include "SandBox.h"
#include "Engine\Components\Transform.h"

namespace Game
{

	int SandBox::Awake()
	{
		std::cout << name << " Awoke" << std::endl;
		GetSceneSystem()->SetScene(name, true, false, false); // set ourselves to active first scene
		return 0;
	}

	int SandBox::Init()
	{
		std::cout << name << " Init" << std::endl;

		// Create an entity
		auto entity = CreateEntity();

		// Add a Transform component with default values
		GetRegistry().emplace<Transform>(entity, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));

		return 0;
	}

	void SandBox::Update(double dt)
	{
		if (GetInputManager()->IsKeyTriggered(VK_SPACE))
		{
			std::cout << "Hit space" << std::endl;
		}
	}

	void SandBox::FixedUpdate(unsigned int tickThisSecond)
	{
		// std::cout << name << " Fixed Update: " << tickThisSecond << std::endl;

		// example for how to iterate all of our entities transforms
		/*
		auto view = GetRegistry().view<Transform>();
		for (auto entity : view)
		{
			const auto& transform = view.get<Transform>(entity);
			std::cout << "Entity Transform - Position: ("
				<< transform.position.x << ", "
				<< transform.position.y << ", "
				<< transform.position.z << "), Scale: ("
				<< transform.scale.x << ", "
				<< transform.scale.y << ", "
				<< transform.scale.z << ")" << std::endl;
		}
		*/
	}

	int SandBox::Exit()
	{
		std::cout << name << " Exiting" << std::endl;

		return 0;
	}

}
