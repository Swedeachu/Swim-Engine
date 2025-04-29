#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
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

	// TODO: the faces of the cube are not UV mapped
	std::pair<std::vector<Engine::Vertex>, std::vector<uint16_t>> MakeCube()
	{
		// 8 unique corners of the cube
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

		// Face definitions with CCW order and associated color
		struct FaceDefinition
		{
			std::array<int, 4> c;
			glm::vec3 color;
		};

		std::array<FaceDefinition, 6> faces = {
			// FRONT (+Z)
			FaceDefinition{{ {4, 5, 6, 7} }, glm::vec3(1.0f, 0.0f, 0.0f)}, // Red

			// BACK (-Z)
			FaceDefinition{{ {1, 0, 3, 2} }, glm::vec3(0.0f, 1.0f, 0.0f)}, // Green

			// LEFT (-X)
			FaceDefinition{{ {0, 4, 7, 3} }, glm::vec3(0.0f, 0.0f, 1.0f)}, // Blue

			// RIGHT (+X)
			FaceDefinition{{ {5, 1, 2, 6} }, glm::vec3(1.0f, 1.0f, 0.0f)}, // Yellow

			// TOP (+Y)
			FaceDefinition{{ {3, 7, 6, 2} }, glm::vec3(1.0f, 0.0f, 1.0f)}, // Magenta

			// BOTTOM (-Y)
			FaceDefinition{{ {4, 0, 1, 5} }, glm::vec3(0.0f, 1.0f, 1.0f)}, // Cyan
		};

		// Define UV coordinates for each vertex of a face
		std::array<glm::vec2, 4> faceUVs = {
				glm::vec2(0.0f, 1.0f), // Bottom-left
				glm::vec2(1.0f, 1.0f), // Bottom-right
				glm::vec2(1.0f, 0.0f), // Top-right
				glm::vec2(0.0f, 0.0f)  // Top-left
		};

		// Build the 24 vertices (4 vertices per face)
		std::vector<Engine::Vertex> vertices;
		vertices.reserve(24);

		for (const auto& face : faces)
		{
			for (int i = 0; i < 4; i++)
			{
				Engine::Vertex v{};
				v.position = corners[face.c[i]];
				v.color = face.color;
				v.uv = faceUVs[i]; // Assign proper UVs
				vertices.push_back(v);
			}
		}

		// Build the 36 indices (6 faces * 6 indices per face)
		std::vector<uint16_t> indices;
		indices.reserve(36);
		for (int faceIdx = 0; faceIdx < 6; faceIdx++)
		{
			uint16_t base = faceIdx * 4;
			// First triangle of the face
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);

			// Second triangle of the face
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
		auto& materialPool = Engine::MaterialPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();

		// Define vertices and indices for a quad mesh
		std::vector<Engine::Vertex> quadVertices = {
			//  position          color               uv
			{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},  // bottom-left  => uv(0,1)
			{{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},  // bottom-right => uv(1,1)
			{{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  // top-right    => uv(1,0)
			{{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}}   // top-left     => uv(0,0)
		};

		std::vector<uint16_t> quadIndices = { 0, 1, 2, 2, 3, 0 };

		// Made a helper since anything 3D is phat
		auto cubeData = MakeCube();

		// Register both meshes
		auto mesh1 = meshPool.RegisterMesh("RainbowCube", cubeData.first, cubeData.second);
		auto mesh2 = meshPool.RegisterMesh("RainbowQuad", quadVertices, quadIndices);

		// Register both material data
		auto materialData1 = materialPool.RegisterMaterialData(
			"alien material", mesh1, texturePool.GetTexture2DLazy("alien")
		);

		auto materialData2 = materialPool.RegisterMaterialData(
			"mart material", mesh2, texturePool.GetTexture2DLazy("mart")
		);

		// Create and set up two entities
		auto& registry = GetRegistry();

		// Entity 1: Controlled by WASD
		controllableEntity = CreateEntity();
		registry.emplace<Engine::Transform>(controllableEntity, glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(1.0f));
		registry.emplace<Engine::Material>(controllableEntity, materialData1);

		// Entity 2: Static entity
		auto staticEntity = CreateEntity();
		registry.emplace<Engine::Transform>(staticEntity, glm::vec3(3.0f, 0.0f, -2.0f), glm::vec3(1.0f));
		registry.emplace<Engine::Material>(staticEntity, materialData2);

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
