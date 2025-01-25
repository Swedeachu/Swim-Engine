#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Textures\TexturePool.h"
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

	std::pair<std::vector<Engine::Vertex>, std::vector<uint16_t>> MakeCube()
	{
		// 8 unique corners
		std::array<glm::vec3, 8> corners = {
				glm::vec3{-0.5f, -0.5f, -0.5f}, // 0
				glm::vec3{ 0.5f, -0.5f, -0.5f}, // 1
				glm::vec3{ 0.5f,  0.5f, -0.5f}, // 2
				glm::vec3{-0.5f,  0.5f, -0.5f}, // 3
				glm::vec3{-0.5f, -0.5f,  0.5f}, // 4
				glm::vec3{ 0.5f, -0.5f,  0.5f}, // 5
				glm::vec3{ 0.5f,  0.5f,  0.5f}, // 6
				glm::vec3{-0.5f,  0.5f,  0.5f}, // 7
		};

		// Face definitions, each face has 4 corners in *CCW* order from outside
		struct FaceDefinition
		{
			std::array<int, 4> c;
			glm::vec3 color;
		};

		// 
		// IMPORTANT: We reorder the corners so they’re CCW as viewed from *outside*:
		//
		std::array<FaceDefinition, 6> faces = {
			// FRONT (z=+0.5): corners 4->5->6->7 is CCW looking down +Z
			FaceDefinition{{ {4, 5, 6, 7} }, glm::vec3(1.0f, 0.0f, 0.0f)}, // Red

			// BACK (z=-0.5): corners 1->0->3->2 is CCW looking down -Z
			//   or equivalently (0->1->2->3) can be clockwise if viewed from outside
			//   So we swap to 1->0->3->2
			FaceDefinition{{ {1, 0, 3, 2} }, glm::vec3(0.0f, 1.0f, 0.0f)}, // Green

			// LEFT (x=-0.5): corners 0->4->7->3 is CCW looking from outside (negative X)
			FaceDefinition{{ {0, 4, 7, 3} }, glm::vec3(0.0f, 0.0f, 1.0f)}, // Blue

			// RIGHT (x=+0.5): corners 5->1->2->6 is CCW looking from outside (positive X)
			//   old code had (1,5,6,2) but that might be out-of-order
			//   we reorder to (5,1,2,6) for consistent CCW from outside
			FaceDefinition{{ {5, 1, 2, 6} }, glm::vec3(1.0f, 1.0f, 0.0f)}, // Yellow

			// TOP (y=+0.5): corners 3->7->6->2 is CCW looking down +Y
			FaceDefinition{{ {3, 7, 6, 2} }, glm::vec3(1.0f, 0.0f, 1.0f)}, // Magenta

			// BOTTOM (y=-0.5): corners 4->0->1->5 is CCW looking down -Y
			//   old code had (0,4,5,1) which might be reversed 
			//   reorder to (4,0,1,5)
			FaceDefinition{{ {4, 0, 1, 5} }, glm::vec3(0.0f, 1.0f, 1.0f)}, // Cyan
		};

		// Build the 24 vertices
		std::vector<Engine::Vertex> vertices;
		vertices.reserve(24);

		for (const auto& face : faces)
		{
			for (int i = 0; i < 4; i++)
			{
				Engine::Vertex v{};
				v.position = corners[face.c[i]];
				v.color = face.color;
				vertices.push_back(v);
			}
		}

		// Build the 36 indices (6 faces * 6)
		std::vector<uint16_t> indices;
		indices.reserve(36);
		for (int faceIdx = 0; faceIdx < 6; faceIdx++)
		{
			uint16_t base = faceIdx * 4;
			// Tri 1: (0,1,2)
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);

			// Tri 2: (2,3,0)
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			indices.push_back(base + 0);
		}

		return { vertices, indices };
	}

	entt::entity controllableEntity;

	int SandBox::Init()
	{
		std::cout << name << " Init" << std::endl;

		// Get the MeshPool instance
		auto& meshPool = Engine::MeshPool::GetInstance();

		// Define vertices and indices for a quad mesh
		std::vector<Engine::Vertex> quadVertices = {
				{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // Red
				{{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // Green
				{{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // Blue
				{{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}  // White
		};
		std::vector<uint16_t> quadIndices = { 0, 1, 2, 2, 3, 0 };

		// Made a helper since anything 3D is phat
		auto cubeData = MakeCube();

		// Register both meshes
		auto mesh1 = meshPool.RegisterMesh("RainbowCube", cubeData.first, cubeData.second);
		auto mesh2 = meshPool.RegisterMesh("RainbowQuad", quadVertices, quadIndices);

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
		auto staticEntitysMaterial = registry.get<Engine::Material>(staticEntity); // lets get the material as we want to set its albedo texture
		staticEntitysMaterial.albedoMap = Engine::TexturePool::GetInstance().GetTexture2DLazy("mart");

		return 0;
	}

	// this is just quickly thrown together demo code, behavior components coming soon
	void SandBox::Update(double dt)
	{
		auto input = GetInputManager();
		auto cameraSystem = GetCameraSystem();
		auto& registry = GetRegistry();

		// ====== Camera Movement (WASD with camera direction) =======
		const float cameraMoveSpeed = 5.0f;
		glm::vec3 cameraMoveDir{ 0.0f };

		// 1. Get the camera's yaw (Y in Euler angles). We'll ignore pitch for movement on XZ plane.
		auto camEuler = cameraSystem->GetCamera().GetRotationEuler();
		float yawRadians = glm::radians(camEuler.y);

		// 2. Build a forward vector pointing where the camera looks in XZ.
		glm::vec3 forwardXZ(
			sin(yawRadians),
			0.0f,
			-cos(yawRadians)
		);

		// 3. Build a right vector perpendicular to forward
		glm::vec3 rightXZ = glm::normalize(glm::cross(forwardXZ, glm::vec3(0, 1, 0)));

		// 4. Check WASD for camera movement
		if (input->IsKeyDown('W'))
		{
			cameraMoveDir += forwardXZ;
		}
		if (input->IsKeyDown('S'))
		{
			cameraMoveDir -= forwardXZ;
		}
		if (input->IsKeyDown('A'))
		{
			cameraMoveDir -= rightXZ;
		}
		if (input->IsKeyDown('D'))
		{
			cameraMoveDir += rightXZ;
		}
		// Vertical camera movement
		if (input->IsKeyDown(VK_SPACE))
		{
			cameraMoveDir += glm::vec3(0, 1, 0);
		}
		if (input->IsKeyDown(VK_SHIFT))
		{
			cameraMoveDir -= glm::vec3(0, 1, 0);
		}

		// 5. Apply camera movement
		if (glm::length(cameraMoveDir) > 0.0f)
		{
			glm::vec3 cameraMoveDelta = glm::normalize(cameraMoveDir) * cameraMoveSpeed * static_cast<float>(dt);
			cameraSystem->GetCamera().SetPosition(cameraSystem->GetCamera().GetPosition() + cameraMoveDelta);
		}

		// ====== Camera Rotation (mouse-based when right-click is down) =======
		if (input->IsKeyDown(VK_RBUTTON))
		{
			float mouseSensitivity = 0.1f;
			auto mouseDelta = input->GetMousePositionDelta();

			auto currentEuler = cameraSystem->GetCamera().GetRotationEuler();
			currentEuler.x += mouseDelta.y * mouseSensitivity;
			currentEuler.y += mouseDelta.x * mouseSensitivity;

			// Clamp pitch, no roll
			if (currentEuler.x > 89.0f)  currentEuler.x = 89.0f;
			if (currentEuler.x < -89.0f) currentEuler.x = -89.0f;

			cameraSystem->GetCamera().SetRotationEuler(
				currentEuler.x,
				currentEuler.y,
				0.0f
			);
		}

		// ====== Controllable Entity Movement (Arrow Keys) =======
		const float entityMoveSpeed = 5.0f;
		glm::vec3 entityMoveDir{ 0.0f };

		// Arrow keys for entity movement
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

		// Apply movement to the controllable entity
		if (glm::length(entityMoveDir) > 0.0f)
		{
			entityMoveDir = glm::normalize(entityMoveDir) * entityMoveSpeed * static_cast<float>(dt);
			auto& transform = registry.get<Engine::Transform>(controllableEntity);
			transform.GetPositionRef() += entityMoveDir;
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
