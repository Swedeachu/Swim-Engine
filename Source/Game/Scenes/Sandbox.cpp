#include "PCH.h"
#include "SandBox.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Mesh.h"

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

		auto entity = CreateEntity();

		// Add Transform component
		GetRegistry().emplace<Engine::Transform>(entity, glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(1.0f, 1.0f, 1.0f));

		// Add Mesh component
		std::vector<Engine::Vertex> vertices = {
				{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
				{{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
				{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
				{{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}
		};
		std::vector<uint16_t> indices = { 0, 1, 2, 2, 3, 0 };

		GetRegistry().emplace<Engine::Mesh>(entity, vertices, indices);

		return 0;
	}

	void SandBox::Update(double dt)
	{
		auto input = GetInputManager();
		auto camera = GetCameraSystem();

		const float moveSpeed = 5.0f; // Speed of movement
		glm::vec3 moveDirection{ 0.0f, 0.0f, 0.0f };

		// Handle input for WASD keys
		if (input->IsKeyDown('W'))
		{
			moveDirection += glm::vec3(0.0f, 0.0f, -1.0f); // Forward
		}
		if (input->IsKeyDown('S'))
		{
			moveDirection += glm::vec3(0.0f, 0.0f, 1.0f); // Backward
		}
		if (input->IsKeyDown('A'))
		{
			moveDirection += glm::vec3(-1.0f, 0.0f, 0.0f); // Left
		}
		if (input->IsKeyDown('D'))
		{
			moveDirection += glm::vec3(1.0f, 0.0f, 0.0f); // Right
		}

		// Normalize direction to ensure consistent speed and apply delta time
		if (glm::length(moveDirection) > 0.0f)
		{
			moveDirection = glm::normalize(moveDirection);

			auto view = GetRegistry().view<Engine::Transform>();
			entt::entity et;
			bool found = false;
			for (auto entity : view)
			{
				et = entity;
				found = true;
				break;
			}

			if (found)
			{
				auto& transform = view.get<Engine::Transform>(et);
				transform.position += moveDirection * moveSpeed * static_cast<float>(dt);
			}

			// camera->camera.position += moveDirection * moveSpeed * static_cast<float>(dt);
			// std::cout << camera->camera.position.x << ", " << camera->camera.position.y << ", " << camera->camera.position.z << std::endl;
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
