#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"
#include "RandomUtils.h"

namespace Game
{

	constexpr static bool doStressTest = true;
	constexpr static bool fullyUniqueCubeMeshes = false; // can take things to a crawl because we aren't doing the mega mesh buffer yet
	constexpr static bool randomizeCubeRotations = true;
	// TODO: make more than just different colored cube meshes (pyramids and spheres)

	constexpr static const int GRID_HALF_SIZE = 10; // for example 10 makes a 20x20x20 cube of cubes
	constexpr static const float SPACING = 3.5f; // 2.5f

	int SandBox::Awake()
	{
		std::cout << name << " Awoke" << std::endl;
		GetSceneSystem()->SetScene(name, true, false, false); // set ourselves to active first scene
		return 0;
	}

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

	std::pair<std::vector<Engine::Vertex>, std::vector<uint16_t>> MakeRandomColoredCube()
	{
		std::array<glm::vec3, 8> corners = {
			glm::vec3{-0.5f, -0.5f, -0.5f},
			glm::vec3{ 0.5f, -0.5f, -0.5f},
			glm::vec3{ 0.5f,  0.5f, -0.5f},
			glm::vec3{-0.5f,  0.5f, -0.5f},
			glm::vec3{-0.5f, -0.5f,  0.5f},
			glm::vec3{ 0.5f, -0.5f,  0.5f},
			glm::vec3{ 0.5f,  0.5f,  0.5f},
			glm::vec3{-0.5f,  0.5f,  0.5f},
		};

		std::array<std::array<int, 4>, 6> faceIndices = {
			std::array<int,4>{4,5,6,7},  // Front
			std::array<int,4>{1,0,3,2},  // Back
			std::array<int,4>{0,4,7,3},  // Left
			std::array<int,4>{5,1,2,6},  // Right
			std::array<int,4>{3,7,6,2},  // Top
			std::array<int,4>{4,0,1,5},  // Bottom
		};

		std::array<glm::vec2, 4> uvs = {
			glm::vec2(0.0f, 1.0f),
			glm::vec2(1.0f, 1.0f),
			glm::vec2(1.0f, 0.0f),
			glm::vec2(0.0f, 0.0f),
		};

		std::vector<Engine::Vertex> vertices;
		std::vector<uint16_t> indices;

		vertices.reserve(24);
		indices.reserve(36);

		for (int face = 0; face < 6; ++face)
		{
			glm::vec3 color = glm::vec3(
				Engine::randFloat(0.2f, 1.0f),
				Engine::randFloat(0.2f, 1.0f),
				Engine::randFloat(0.2f, 1.0f)
			);

			for (int i = 0; i < 4; ++i)
			{
				Engine::Vertex v{};
				v.position = corners[faceIndices[face][i]];
				v.uv = uvs[i];
				v.color = color;
				vertices.push_back(v);
			}

			uint16_t base = face * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			indices.push_back(base + 0);
		}

		return { vertices, indices };
	}

	std::shared_ptr<Engine::MaterialData> RegisterRandomMaterial(
		const std::shared_ptr<Engine::Mesh>& mesh,
		int index)
	{
		auto& materialPool = Engine::MaterialPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();

		std::string matName = "mat_" + std::to_string(index);

		// 33% mart, 33% alien, 33% no texture
		int choice = Engine::randInt(0, 2);
		std::shared_ptr<Engine::Texture2D> tex = nullptr;

		if (choice == 0)
		{
			tex = texturePool.GetTexture2DLazy("mart");
		}
		else if (choice == 1)
		{
			tex = texturePool.GetTexture2DLazy("alien");
		}

		return materialPool.RegisterMaterialData(matName, mesh, tex);
	}

	void MakeTonsOfRandomPositionedEntities()
	{
		auto& registry = Engine::SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene()->GetRegistry();
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();

		const int total = (GRID_HALF_SIZE * 2 + 1);

		if constexpr (!fullyUniqueCubeMeshes)
		{
			// If we aren't randomizing every cube's mesh to be unique then we will just have some regular ones to choose from
			auto cubeData = MakeCube();
			auto sharedMesh = meshPool.RegisterMesh("SharedCube", cubeData.first, cubeData.second);

			materialPool.RegisterMaterialData("RegularCube", sharedMesh); // this one has no texture (colored mesh)
			materialPool.RegisterMaterialData("MartCube", sharedMesh, texturePool.GetTexture2DLazy("mart"));
			materialPool.RegisterMaterialData("AlienCube", sharedMesh, texturePool.GetTexture2DLazy("alien"));
		}

		// === Generate entities ===
		for (int x = -GRID_HALF_SIZE; x <= GRID_HALF_SIZE; ++x)
		{
			for (int y = -GRID_HALF_SIZE; y <= GRID_HALF_SIZE; ++y)
			{
				for (int z = -GRID_HALF_SIZE; z <= GRID_HALF_SIZE; ++z)
				{
					std::shared_ptr<Engine::Mesh> mesh;
					std::shared_ptr<Engine::MaterialData> material;

					if constexpr (fullyUniqueCubeMeshes)
					{
						// This literally makes every mesh unique for all cubes (super cancer but meant to be a stress test)
						auto cubeData = MakeRandomColoredCube();
						std::string meshName = "cube_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
						mesh = meshPool.RegisterMesh(meshName, cubeData.first, cubeData.second);

						int seed = Engine::randInt(0, 999999);
						material = RegisterRandomMaterial(mesh, seed);
					}
					else
					{
						// Pick material to use randomly but not create a million new random ones (33% mart, 33% alien, 33% no texture)
						int matID = Engine::randInt(0, 2);
						if (matID == 0)
						{
							material = materialPool.GetMaterialData("RegularCube");
						}
						else if (matID == 1)
						{
							material = materialPool.GetMaterialData("MartCube");
						}
						else if (matID == 2)
						{
							material = materialPool.GetMaterialData("AlienCube");
						}
					}

					entt::entity entity = registry.create();

					glm::vec3 pos = glm::vec3(
						x * SPACING,
						y * SPACING,
						z * SPACING
					);

					if constexpr (randomizeCubeRotations)
					{
						float pitch = Engine::randFloat(0.0f, 360.0f); // X
						float yaw = Engine::randFloat(0.0f, 360.0f); // Y
						float roll = Engine::randFloat(0.0f, 360.0f); // Z
						glm::quat rotation = glm::quat(glm::radians(glm::vec3(pitch, yaw, roll)));
						registry.emplace<Engine::Transform>(entity, pos, glm::vec3(1.0f), rotation);
					}
					else
					{
						// Otherwise just use default ctor parameter which is the identity quaternion 
						registry.emplace<Engine::Transform>(entity, pos, glm::vec3(1.0f));
					}

					registry.emplace<Engine::Material>(entity, material);
				}
			}
		}
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

		// The real stress test
		if constexpr (doStressTest) MakeTonsOfRandomPositionedEntities();

		return 0;
	}

	// Draws a wire frame box at the center of the world
	static void WireframeTest(Engine::Scene* scene)
	{
		auto* drawer = scene->GetSceneDebugDraw();
		drawer->SubmitWireframeBox(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.0f));
	}


	// Forward declarations
	void HandleCameraControls(float dt, std::shared_ptr<Engine::InputManager> input, std::shared_ptr<Engine::CameraSystem> cameraSystem);
	void HandleEntityControls(float dt, std::shared_ptr<Engine::InputManager> input, entt::registry& registry, entt::entity controllableEntity);

	// this is just quickly thrown together demo code, behavior components coming soon
	void SandBox::Update(double dt)
	{
		std::shared_ptr<Engine::InputManager> input = GetInputManager();
		std::shared_ptr<Engine::CameraSystem> cameraSystem = GetCameraSystem();
		auto& registry = GetRegistry();

		HandleCameraControls(dt, input, cameraSystem);
		HandleEntityControls(dt, input, registry, controllableEntity);
	}

	// =================== CAMERA CONTROL ===================

	void HandleCameraControls(float dt, std::shared_ptr<Engine::InputManager> input, std::shared_ptr<Engine::CameraSystem> cameraSystem)
	{
		// Movement speed constants
		const float cameraMoveSpeed = 5.0f;
		const float boostMultiplier = 3.0f;
		const float mouseSensitivity = 0.1f;

		// Get the active camera
		auto& camera = cameraSystem->GetCamera();
		glm::vec3 position = camera.GetPosition();

		// these will be stores in the camera controller component 
		static float yaw = 0.0f;    // rotation around world Y axis
		static float pitch = 0.0f;  // rotation around local X axis

		// ----- Mouse Look -----
		if (input->IsKeyDown(VK_RBUTTON))
		{
			glm::vec2 mouseDelta = input->GetMousePositionDelta();

			// Update yaw (horizontal) and pitch (vertical)
			yaw += mouseDelta.x * mouseSensitivity;
			pitch -= mouseDelta.y * mouseSensitivity;

			// Clamp pitch to prevent flipping
			pitch = glm::clamp(pitch, -89.9f, 89.9f);

			// Yaw is around global Y
			glm::quat qYaw = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));

			// Pitch is around camera's local X
			glm::quat qPitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));

			// Combined rotation: yaw first, then pitch
			glm::quat rotation = qYaw * qPitch;

			camera.SetRotation(rotation);
		}

		glm::quat rotation = camera.GetRotation(); // Get updated rotation

		// ----- Movement -----
		glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
		glm::vec3 forward = glm::normalize(rotationMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
		glm::vec3 right = glm::normalize(rotationMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f); // World up

		glm::vec3 movement(0.0f);

		if (input->IsKeyDown('W'))
		{
			movement += forward;
		}
		if (input->IsKeyDown('S'))
		{
			movement -= forward;
		}
		if (input->IsKeyDown('A'))
		{
			movement -= right;
		}
		if (input->IsKeyDown('D'))
		{
			movement += right;
		}
		if (input->IsKeyDown(VK_SPACE))
		{
			movement += up;
		}
		if (input->IsKeyDown(VK_SHIFT))
		{
			movement -= up;
		}

		if (glm::length(movement) > 0.0f)
		{
			movement = glm::normalize(movement);
		}

		float speed = cameraMoveSpeed;
		if (input->IsKeyDown(VK_CONTROL))
		{
			speed *= boostMultiplier;
		}

		position += movement * speed * dt;
		camera.SetPosition(position);
	}

	// =================== ENTITY CONTROL ===================

	void HandleEntityControls(float dt, std::shared_ptr<Engine::InputManager> input, entt::registry& registry, entt::entity controllableEntity)
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
			auto& transform = registry.get<Engine::Transform>(controllableEntity);
			transform.GetPositionRef() += entityMoveDir;
		}
	}

	void SandBox::FixedUpdate(unsigned int tickThisSecond)
	{

	}

	int SandBox::Exit()
	{
		std::cout << name << " Exiting" << std::endl;
		return Scene::Exit();
	}

}
