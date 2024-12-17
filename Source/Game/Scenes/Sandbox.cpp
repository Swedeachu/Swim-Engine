#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Meshes\MeshPool.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"

namespace Game
{

	int SandBox::Awake()
	{
		std::cout << name << " Awoke" << std::endl;
		GetSceneSystem()->SetScene(name, true, false, false); // set ourselves to active first scene
		return 0;
	}

	entt::entity controllableEntity;

	int SandBox::Init()
	{
		std::cout << name << " Init" << std::endl;

		// Get the MeshPool instance
		auto& meshPool = Engine::MeshPool::GetInstance();

		// Define vertices and indices for two quads
		std::vector<Engine::Vertex> quadVertices1 = {
				{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // Red
				{{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // Green
				{{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // Blue
				{{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}  // White
		};

		std::vector<Engine::Vertex> quadVertices2 = {
				{{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 1.0f}}, // Cyan
				{{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 1.0f}}, // Magenta
				{{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 0.0f}}, // Yellow
				{{-0.5f,  0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}}  // Gray
		};

		std::vector<uint16_t> indices = { 0, 1, 2, 2, 3, 0 };

		// Register both meshes
		auto mesh1 = meshPool.RegisterMesh("RainbowQuad1", quadVertices1, indices);
		auto mesh2 = meshPool.RegisterMesh("RainbowQuad2", quadVertices2, indices);

		// Create and set up two entities
		auto& registry = GetRegistry();

		// Entity 1: Controlled by WASD
		controllableEntity = CreateEntity();
		registry.emplace<Engine::Transform>(controllableEntity, glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(1.0f));
		registry.emplace<Engine::Material>(controllableEntity, mesh1);

		// Entity 2: Static entity
		auto staticEntity = CreateEntity();
		registry.emplace<Engine::Transform>(staticEntity, glm::vec3(3.0f, 0.0f, -2.0f), glm::vec3(1.0f));
		registry.emplace<Engine::Material>(staticEntity, mesh2);

		return 0;
	}

	// this is just quickly thrown together demo code, behavior components coming soon
	void SandBox::Update(double dt)
	{
		auto input = GetInputManager();
		auto cameraSystem = GetCameraSystem();
		auto& registry = GetRegistry();

		// ====== Entity Movement (WASD) =======
		const float moveSpeed = 5.0f;
		glm::vec3 moveDirection{ 0.0f };

		if (input->IsKeyDown('W')) { moveDirection += glm::vec3(0.0f, 0.0f, -1.0f); }
		if (input->IsKeyDown('S')) { moveDirection += glm::vec3(0.0f, 0.0f, 1.0f); }
		if (input->IsKeyDown('A')) { moveDirection += glm::vec3(-1.0f, 0.0f, 0.0f); }
		if (input->IsKeyDown('D')) { moveDirection += glm::vec3(1.0f, 0.0f, 0.0f); }

		if (glm::length(moveDirection) > 0.0f)
		{
			moveDirection = glm::normalize(moveDirection) * moveSpeed * static_cast<float>(dt);
			auto& transform = registry.get<Engine::Transform>(controllableEntity);
			transform.GetPositionRef() += moveDirection;
		}

		// ====== Camera Movement (Arrow Keys + Q/E, Z/X) =======
		const float cameraMoveSpeed = 5.0f;
		const float rotationSpeed = 45.0f;

		// Camera position movement
		glm::vec3 cameraMove{ 0.0f };

		if (input->IsKeyDown(VK_UP)) { cameraMove += glm::vec3(0.0f, 0.0f, -1.0f); }
		if (input->IsKeyDown(VK_DOWN)) { cameraMove += glm::vec3(0.0f, 0.0f, 1.0f); }
		if (input->IsKeyDown(VK_LEFT)) { cameraMove += glm::vec3(-1.0f, 0.0f, 0.0f); }
		if (input->IsKeyDown(VK_RIGHT)) { cameraMove += glm::vec3(1.0f, 0.0f, 0.0f); }

		if (glm::length(cameraMove) > 0.0f)
		{
			cameraMove = glm::normalize(cameraMove) * cameraMoveSpeed * static_cast<float>(dt);
			cameraSystem->GetCamera().SetPosition(cameraSystem->GetCamera().GetPosition() + cameraMove);
		}

		// Camera pitch (Q/E) and yaw (Z/X)
		if (input->IsKeyDown('Q'))
		{
			cameraSystem->GetCamera().SetRotationEuler(
				cameraSystem->GetCamera().GetRotationEuler().x + rotationSpeed * static_cast<float>(dt),
				cameraSystem->GetCamera().GetRotationEuler().y,
				0.0f
			);
		}

		if (input->IsKeyDown('E'))
		{
			cameraSystem->GetCamera().SetRotationEuler(
				cameraSystem->GetCamera().GetRotationEuler().x - rotationSpeed * static_cast<float>(dt),
				cameraSystem->GetCamera().GetRotationEuler().y,
				0.0f
			);
		}

		if (input->IsKeyDown('Z'))
		{
			cameraSystem->GetCamera().SetRotationEuler(
				cameraSystem->GetCamera().GetRotationEuler().x,
				cameraSystem->GetCamera().GetRotationEuler().y - rotationSpeed * static_cast<float>(dt),
				0.0f
			);
		}

		if (input->IsKeyDown('X'))
		{
			cameraSystem->GetCamera().SetRotationEuler(
				cameraSystem->GetCamera().GetRotationEuler().x,
				cameraSystem->GetCamera().GetRotationEuler().y + rotationSpeed * static_cast<float>(dt),
				0.0f
			);
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
		return Scene::Exit();
	}

}
