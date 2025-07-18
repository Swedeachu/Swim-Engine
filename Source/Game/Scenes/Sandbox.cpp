#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"
#include "Engine\Components\CompositeMaterial.h"
#include "Engine/Components/MeshDecorator.h"
#include "Library/glm/vec4.hpp"
#include "RandomUtils.h"
#include "Engine\Systems\Entity\EntityFactory.h"
#include "Game\Behaviors\CameraControl\EditorCamera.h"
#include "Game\Behaviors\Demo\SimpleMovement.h"
#include "Game\Behaviors\Demo\CubeMapControlTest.h"
#include "Game\Behaviors\Demo\Spin.h"
#include "Game\Behaviors\Demo\MouseInputDemoBehavior.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Game
{

	constexpr static bool doStressTest = false;
	constexpr static bool doUI = false;
	constexpr static bool doSponza = true;

	constexpr static bool fullyUniqueMeshes = false;
	constexpr static bool randomizeCubeRotations = true;
	constexpr static bool doRandomBehaviors = true;

	constexpr static const int GRID_HALF_SIZE = 10; // for example 10 makes a 20x20x20 cube of cubes
	constexpr static const float SPACING = 3.5f;

	int SandBox::Awake()
	{
		std::cout << name << " Awoke" << std::endl;
		GetSceneSystem()->SetScene(name, true, false, false); // set ourselves to active first scene
		return 0;
	}

	std::pair<std::vector<Engine::Vertex>, std::vector<uint32_t>> MakeCube()
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
		std::vector<uint32_t> indices;
		indices.reserve(36);
		for (int faceIdx = 0; faceIdx < 6; faceIdx++)
		{
			uint32_t base = faceIdx * 4;
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

	std::pair<std::vector<Engine::Vertex>, std::vector<uint32_t>> MakeRandomColoredCube()
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
		std::vector<uint32_t> indices;

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

			uint32_t base = face * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			indices.push_back(base + 0);
		}

		return { vertices, indices };
	}

	std::pair<std::vector<Engine::Vertex>, std::vector<uint32_t>> MakeSphere(
		int latitudeSegments,
		int longitudeSegments,
		glm::vec3 colorTop,
		glm::vec3 colorMid,
		glm::vec3 colorBottom)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		// Clamp to minimum sensible values
		latitudeSegments = std::max(3, latitudeSegments);
		longitudeSegments = std::max(3, longitudeSegments);

		// Generate all vertices
		for (int lat = 0; lat <= latitudeSegments; ++lat)
		{
			float v = static_cast<float>(lat) / latitudeSegments; // [0,1]
			float theta = glm::pi<float>() * v;                        // [0, pi]

			float sinTheta = std::sin(theta);
			float cosTheta = std::cos(theta);

			for (int lon = 0; lon <= longitudeSegments; ++lon)
			{
				float u = static_cast<float>(lon) / longitudeSegments; // [0,1]
				float phi = glm::two_pi<float>() * u;                    // [0, 2pi]

				float sinPhi = std::sin(phi);
				float cosPhi = std::cos(phi);

				glm::vec3 pos;
				pos.x = sinTheta * cosPhi;
				pos.y = cosTheta;
				pos.z = sinTheta * sinPhi;

				glm::vec3 color;

				// Interpolate top -> mid -> bottom along v (Y)
				if (v < 0.5f)
				{
					float t = v * 2.0f;
					color = glm::mix(colorTop, colorMid, t);
				}
				else
				{
					float t = (v - 0.5f) * 2.0f;
					color = glm::mix(colorMid, colorBottom, t);
				}

				// glm::vec2 uv = glm::vec2(u, 1.0f - v);
				glm::vec2 uv = glm::vec2(u, v);

				Engine::Vertex vert;
				vert.position = pos * 0.5f; // Unit sphere scaled to [-0.5, 0.5]
				vert.color = color;
				vert.uv = uv;

				vertices.push_back(vert);
			}
		}

		// Generate indices (CCW winding)
		for (int lat = 0; lat < latitudeSegments; ++lat)
		{
			for (int lon = 0; lon < longitudeSegments; ++lon)
			{
				int current = lat * (longitudeSegments + 1) + lon;
				int next = current + longitudeSegments + 1;

				// Triangle 1 (CCW)
				indices.push_back(static_cast<uint32_t>(current));
				indices.push_back(static_cast<uint32_t>(current + 1));
				indices.push_back(static_cast<uint32_t>(next));

				// Triangle 2 (CCW)
				indices.push_back(static_cast<uint32_t>(current + 1));
				indices.push_back(static_cast<uint32_t>(next + 1));
				indices.push_back(static_cast<uint32_t>(next));
			}
		}

		return { vertices, indices };
	}

	std::pair<std::vector<Engine::Vertex>, std::vector<uint32_t>> GenerateCircleMesh(
		float radius = 0.5f,
		uint32_t segmentCount = 64,
		const glm::vec3& color = { 1.0f, 1.0f, 1.0f }
	)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		// Add center vertex of triangle fan
		vertices.push_back({
				{0.0f, 0.0f, 0.0f}, // Center position
				color,              // Uniform color
				{0.5f, 0.5f}        // Center UV
			});

		// Add outer circle vertices
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			float angle = (float)i / (float)segmentCount * 2.0f * M_PI;

			float x = radius * cosf(angle);
			float y = radius * sinf(angle);

			// UV mapped from [-radius, radius] => [0,1]
			float u = 0.5f + (x / (radius * 2.0f));
			float v = 0.5f - (y / (radius * 2.0f));  // Flip Y for typical UVs

			vertices.push_back({
					{x, y, 0.0f},
					color,
					{u, v}
				});
		}

		// Generate indices for triangle fan
		for (uint32_t i = 1; i <= segmentCount; ++i)
		{
			indices.push_back(0);        // Center
			indices.push_back(i);        // Current vertex
			indices.push_back(i + 1);    // Next vertex (wraps around)
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

	void MakeTonsOfRandomPositionedEntities(Engine::Scene* scene)
	{
		auto& registry = Engine::SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene()->GetRegistry();
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();

		const int total = (GRID_HALF_SIZE * 2 + 1);

		// === Shared Assets Setup ===
		std::vector<std::shared_ptr<Engine::MaterialData>> sharedBarrelMaterials;

		if constexpr (!fullyUniqueMeshes)
		{
			// Shared Cube
			auto cubeData = MakeCube();
			auto sharedCube = meshPool.RegisterMesh("SharedCube", cubeData.first, cubeData.second);
			materialPool.RegisterMaterialData("RegularCube", sharedCube);
			materialPool.RegisterMaterialData("MartCube", sharedCube, texturePool.GetTexture2DLazy("mart"));
			materialPool.RegisterMaterialData("AlienCube", sharedCube, texturePool.GetTexture2DLazy("alien"));

			// Shared Sphere
			auto sphereData = MakeSphere(16, 32, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			auto sharedSphere = meshPool.RegisterMesh("SharedSphere", sphereData.first, sphereData.second);
			materialPool.RegisterMaterialData("RegularSphere", sharedSphere);
			materialPool.RegisterMaterialData("MartSphere", sharedSphere, texturePool.GetTexture2DLazy("mart"));
			materialPool.RegisterMaterialData("AlienSphere", sharedSphere, texturePool.GetTexture2DLazy("alien"));

			// Shared Barrel (composite)
			sharedBarrelMaterials = materialPool.LoadAndRegisterCompositeMaterialFromGLB("Assets/Models/barrel.glb");
		}

		for (int x = -GRID_HALF_SIZE; x <= GRID_HALF_SIZE; ++x)
		{
			for (int y = -GRID_HALF_SIZE; y <= GRID_HALF_SIZE; ++y)
			{
				for (int z = -GRID_HALF_SIZE; z <= GRID_HALF_SIZE; ++z)
				{
					entt::entity entity = registry.create();

					glm::vec3 pos = glm::vec3(x * SPACING, y * SPACING, z * SPACING);

					if constexpr (randomizeCubeRotations)
					{
						glm::vec3 euler = Engine::randVec3(0.0f, 360.0f);
						glm::quat rot = glm::quat(glm::radians(euler));
						registry.emplace<Engine::Transform>(entity, pos, glm::vec3(1.0f), rot);
					}
					else
					{
						registry.emplace<Engine::Transform>(entity, pos, glm::vec3(1.0f));
					}

					// === Random Mesh Type: 0 = Sphere, 1 = Cube, 2 = Barrel ===
					int choice = Engine::randInt(0, 2);

					if (choice == 0) // Sphere
					{
						if constexpr (fullyUniqueMeshes)
						{
							int latSegments = Engine::randInt(8, 24);
							int longSegments = Engine::randInt(16, 48);
							glm::vec3 top = Engine::randVec3(0.2f, 1.0f);
							glm::vec3 mid = Engine::randVec3(0.2f, 1.0f);
							glm::vec3 bot = Engine::randVec3(0.2f, 1.0f);

							auto sphereData = MakeSphere(latSegments, longSegments, top, mid, bot);
							std::string name = "sphere_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
							auto mesh = meshPool.RegisterMesh(name, sphereData.first, sphereData.second);
							auto material = RegisterRandomMaterial(mesh, Engine::randInt(0, 999999));
							registry.emplace<Engine::Material>(entity, material);
						}
						else
						{
							auto mesh = meshPool.GetMesh("SharedSphere");
							int matID = Engine::randInt(0, 2);
							std::string matName = (matID == 0) ? "RegularSphere" : (matID == 1) ? "MartSphere" : "AlienSphere";
							auto material = materialPool.GetMaterialData(matName);
							registry.emplace<Engine::Material>(entity, material);
						}
					}
					else if (choice == 1) // Cube
					{
						if constexpr (fullyUniqueMeshes)
						{
							auto cubeData = MakeRandomColoredCube();
							std::string name = "cube_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
							auto mesh = meshPool.RegisterMesh(name, cubeData.first, cubeData.second);
							auto material = RegisterRandomMaterial(mesh, Engine::randInt(0, 999999));
							registry.emplace<Engine::Material>(entity, material);
						}
						else
						{
							auto mesh = meshPool.GetMesh("SharedCube");
							int matID = Engine::randInt(0, 2);
							std::string matName = (matID == 0) ? "RegularCube" : (matID == 1) ? "MartCube" : "AlienCube";
							auto material = materialPool.GetMaterialData(matName);
							registry.emplace<Engine::Material>(entity, material);
						}
					}
					else // Barrel (always shared)
					{
						registry.emplace<Engine::CompositeMaterial>(entity, Engine::CompositeMaterial(sharedBarrelMaterials));
						Engine::Transform& trans = registry.get<Engine::Transform>(entity);
						trans.SetScale(glm::vec3(0.2f));
					}

					// Optional spin behavior
					if constexpr (doRandomBehaviors)
					{
						if (Engine::randInt(0, 1) == 0)
						{
							scene->AddBehavior<Game::Spin>(entity, Engine::randFloat(25.0f, 90.0f));
						}
					}
				}
			}
		}
	}

	void MakeUI(Engine::Scene* scene)
	{
		// Get the MeshPool instance
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();
		auto engine = Engine::SwimEngine::GetInstance();

		// Define vertices and indices for a white quad mesh
		std::vector<Engine::Vertex> quadVertices = {
			//  position          color               uv
			{{-0.5f, -0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // bottom-left  => uv(0,1)
			{{ 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // bottom-right => uv(1,1)
			{{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // top-right    => uv(1,0)
			{{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}}   // top-left     => uv(0,0)
		};

		std::vector<uint32_t> quadIndices = { 0, 1, 2, 2, 3, 0 };

		auto whiteQuad = meshPool.RegisterMesh("WhiteQuad", quadVertices, quadIndices);
		auto whiteMaterial = materialPool.RegisterMaterialData("WhiteMaterial", whiteQuad, texturePool.GetTexture2DLazy("mart"));

		auto whiteEntity = scene->CreateEntity();

		// Place it in the screen
		glm::vec3 whiteEntityScreenPos = glm::vec3(300, 900, 0.0f);

		// Pixel size
		glm::vec3 whiteEntitySize = glm::vec3(300.0f, 150.0f, 1.0f);

		scene->AddComponent<Engine::Transform>(whiteEntity, Engine::Transform(whiteEntityScreenPos, whiteEntitySize, glm::quat(), Engine::TransformSpace::Screen));
		scene->AddComponent<Engine::Material>(whiteEntity, Engine::Material(whiteMaterial));
		scene->AddBehavior<MouseInputDemoBehavior>(whiteEntity); // with a behavior to demonstrate mouse input callbacks

		Engine::MeshDecorator decorator = Engine::MeshDecorator(
			glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),    // fill
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),    // stroke
			glm::vec2(16.0f, 16.0f),              // stroke width X/Y
			glm::vec2(32.0f, 32.0f),              // corner radius X/Y
			glm::vec2(4.0f),                      // padding
			true, true, true, true                // to enable: rounded, stroke, fill, material texture
		);

		scene->AddComponent<Engine::MeshDecorator>(whiteEntity, decorator);

		// Below here is a bunch of bools for messing with making a second UI entity to test stuff out with

		constexpr bool makeSecondEntity = true;
		if constexpr (!makeSecondEntity) return;

		// Create the red entity just to prove we can do multiple UI at a time like any entity
		auto redEntity = scene->CreateEntity();

		// Position it below the white entity
		glm::vec3 redEntityScreenPos = glm::vec3(300, 700, 0.0f);

		// Give it a different size
		glm::vec3 redEntitySize = glm::vec3(250.0f, 100.0f, 1.0f);

		// Add transform component to position and size the red entity on screen
		scene->AddComponent<Engine::Transform>(
			redEntity,
			Engine::Transform(redEntityScreenPos, redEntitySize, glm::quat(), Engine::TransformSpace::Screen)
		);

		constexpr bool useDifferentMaterial = true;

		if constexpr (useDifferentMaterial)
		{
			std::shared_ptr<Engine::Mesh> secondMesh = nullptr;
			constexpr bool isCircle = false;
			if constexpr (isCircle)
			{
				auto [circleVertices, circleIndices] = GenerateCircleMesh(1.0f, 128, { 1.0f, 0.0f, 0.0f });
				secondMesh = meshPool.RegisterMesh("SecondTestMeshUI", circleVertices, circleIndices);
			}
			else
			{
				std::vector<Engine::Vertex> redQuadVertices = {
					//  position          color               uv
					{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},  // bottom-left  => uv(0,1)
					{{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},  // bottom-right => uv(1,1)
					{{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  // top-right    => uv(1,0)
					{{-0.5f,  0.5f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}   // top-left     => uv(0,0)
				};
				secondMesh = meshPool.RegisterMesh("SecondTestMeshUI", redQuadVertices, quadIndices);
			}

			constexpr bool useTex = true;
			auto tex = (useTex) ? texturePool.GetTexture2DLazy("alien") : nullptr;
			auto secondMaterial = materialPool.RegisterMaterialData("SecondMaterial", secondMesh, tex);
			scene->AddComponent<Engine::Material>(redEntity, Engine::Material(secondMaterial));
		}
		else
		{
			// Use the same material as whiteEntity
			scene->AddComponent<Engine::Material>(redEntity, Engine::Material(whiteMaterial));
		}

		// Decorator with red stroke instead of black
		Engine::MeshDecorator redDecorator = Engine::MeshDecorator(
			glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),    // fill
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),    // stroke
			glm::vec2(12.0f, 12.0f),              // stroke width X/Y (slightly thinner)
			glm::vec2(16.0f, 16.0f),              // corner radius X/Y (smaller rounding)
			glm::vec2(4.0f),                      // padding
			true, true, true, false               // rounded, stroke, fill, material texture
		);

		redDecorator.SetUseMeshMaterialColor(true);

		// Apply the decorator to the red entity
		scene->AddComponent<Engine::MeshDecorator>(redEntity, redDecorator);
	}

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

		std::vector<uint32_t> quadIndices = { 0, 1, 2, 2, 3, 0 };

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

		auto sphereData = MakeSphere(
			24, 48,
			glm::vec3(1, 0, 0),   // top: red
			glm::vec3(1, 1, 0),   // mid: yellow
			glm::vec3(0, 0, 1)    // bottom: blue
		);

		auto sphereMesh = meshPool.RegisterMesh("Sphere", sphereData.first, sphereData.second);

		auto sphereDataMaterial = materialPool.RegisterMaterialData("sphere material", sphereMesh);

		// We are going to use the entity factory now for a little bit easier physical entity creation (transform and material entities)
		Engine::EntityFactory& entityFactory = Engine::EntityFactory::GetInstance();

		// Make a static quad entity in world space but with UI decorator on it, essentially a bill board (TODO: billboard behavior to always face the camera via rotation)
		auto billboard = CreateEntity();
		AddComponent<Engine::Transform>(billboard, Engine::Transform(glm::vec3(3.0f, 0.0f, -2.0f), glm::vec3(1.0f)));
		AddComponent<Engine::Material>(billboard, materialData2);
		// AddComponent<Engine::Material>(billboard, sphereDataMaterial); // extreme trollage
		// AddComponent<Engine::Material>(billboard, materialData1); // cube 3D mesh with UI decorators on it

		///* World space UI 
		Engine::MeshDecorator billboardDecorator = Engine::MeshDecorator(
			glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),    // fill: green
			glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),    // stroke: red
			glm::vec2(16.0f, 16.0f),              // stroke width X/Y (slightly thinner)
			glm::vec2(16.0f, 16.0f),              // corner radius X/Y (smaller rounding)
			glm::vec2(4.0f),                      // padding
			true, true, true, false               // rounded, stroke, fill, use material texture
		);
		// billboardDecorator.SetUseMeshMaterialColor(true);
		AddComponent<Engine::MeshDecorator>(billboard, billboardDecorator);
		AddBehavior<Spin>(billboard);
		//*/

		// Make sphere entity
		entityFactory.CreateWithTransformAndMaterial(
			Engine::Transform(glm::vec3(-2.0f, 0.0f, -2.0f), glm::vec3(1.0f)),
			Engine::Material(sphereDataMaterial)
		);

		int textureCountBefore = Engine::Texture2D::GetTextureCountOnGPU();

		// Make a barrel entity that spins
		auto spinEntity = CreateEntity();
		AddComponent<Engine::Transform>(spinEntity, Engine::Transform(glm::vec3(6.0f, 0.0f, -2.0f), glm::vec3(1.0f)));

		auto barrelModel = materialPool.LazyLoadAndGetCompositeMaterial("Assets/Models/barrel.glb");

		AddComponent<Engine::CompositeMaterial>(spinEntity, Engine::CompositeMaterial(barrelModel));
		AddBehavior<Game::Spin>(spinEntity, 90.0f); // 90 degrees per second

		// We can make the Movement entity like this (actual physical entity we can control with WASD simple controller)
		entityFactory.CreateWithTransformMaterialAndBehaviors<SimpleMovement>(
			Engine::Transform(glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(1.0f)),
			Engine::Material(materialData1)
		);

		// We can load scene scripts this way as a cool hack/trick
		entityFactory.CreateWithBehaviors<EditorCamera, CubeMapControlTest>(); // Makes an empty entity in the scene with these scripts on it (we can do this with as many behaviors as we want)

		constexpr bool glbTests = true;
		if constexpr (!glbTests) return 0;

		// Couch time
		auto couch = CreateEntity();
		AddComponent<Engine::Transform>(couch, Engine::Transform(glm::vec3(-6.0f, 0.0f, -2.0f), glm::vec3(1.0f)));
		auto sofaModel = materialPool.LazyLoadAndGetCompositeMaterial("Assets/Models/webp_sofa.glb");
		AddComponent<Engine::CompositeMaterial>(couch, Engine::CompositeMaterial(sofaModel));

		// Sponza 3D model test
		if constexpr (doSponza)
		{
			std::vector<std::shared_ptr<Engine::MaterialData>> sponzaData;
			std::cout << "Sponza load time\n";

			// unpacked raw version that is much easier to parse, but not efficent + fat on disk (deleted from repo it was so fat)
			// sponzaData = materialPool.LoadAndRegisterCompositeMaterialFromGLB("Assets/Models/Sponza/Raw/sponza.glb"); // 156 MB
			
			// compressed version + using ktx for textures, efficent
			// sponzaData = materialPool.LoadAndRegisterCompositeMaterialFromGLB("Assets/Models/Sponza/sponza-ktx.glb"); // 15 MB

			// super compressed draco version, very efficent and fast, perfect for release
			sponzaData = materialPool.LoadAndRegisterCompositeMaterialFromGLB("Assets/Models/Sponza/sponza-ktx-draco.glb"); // 9 MB

			glm::vec3 sponzaScale = glm::vec3(1.0f);

			if (materialPool.CompositeMaterialExists("Assets/Models/barrel.glb") && sponzaData.empty()) // if barrel exists and sponza wasn't loaded, set the data to the barrel
			{
				sponzaData = materialPool.GetCompositeMaterialData("Assets/Models/barrel.glb");
			}
			else if (sponzaData.empty()) // if barrel doesn't exist and sponza wasnt loaded, load and set the data to the barrel
			{
				sponzaData = materialPool.LoadAndRegisterCompositeMaterialFromGLB("Assets/Models/barrel.glb");
			}

			Engine::CompositeMaterial sponzaCompositeMaterial = Engine::CompositeMaterial(sponzaData);

			auto sponza = CreateEntity();
			AddComponent<Engine::Transform>(sponza, Engine::Transform(glm::vec3(3.0f, 0.0f, -12.0f), sponzaScale));
			AddComponent<Engine::CompositeMaterial>(sponza, sponzaCompositeMaterial);
		}

		int textureCountAfter = Engine::Texture2D::GetTextureCountOnGPU();

		std::cout << "[Scene] Textures before GLB load: " << textureCountBefore << " | After: " << textureCountAfter << std::endl;

		// The real stress test
		if constexpr (doStressTest) MakeTonsOfRandomPositionedEntities(this);

		if constexpr (doUI) MakeUI(this);

		return 0;
	}

	// Draws a wire frame box at the center of the world
	static void WireframeTest(Engine::Scene* scene)
	{
		auto* drawer = scene->GetSceneDebugDraw();
		drawer->SubmitWireframeBox(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.0f));
	}

	void SandBox::Update(double dt)
	{
		// WireframeTest(this);
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
