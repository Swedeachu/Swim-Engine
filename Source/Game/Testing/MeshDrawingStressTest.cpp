#include "PCH.h"
#include "MeshDrawingStressTest.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
#include "Engine\Systems\Renderer\Core\Meshes\PrimitiveMeshes.h"
#include "RandomUtils.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"
#include "Engine\Components\CompositeMaterial.h"
#include "Game\Behaviors\Demo\Spin.h"

namespace Game
{

	constexpr static const int GRID_HALF_SIZE = 10; // for example 10 makes a 20x20x20 2D grid
	constexpr static const float SPACING = 3.5f;

	constexpr static bool fullyUniqueMeshes = false;
	constexpr static bool randomizeCubeRotations = true;
	constexpr static bool doRandomBehaviors = true;

	std::shared_ptr<Engine::LegacyRenderBinding> RegisterRandomMaterial
	(
		Engine::MaterialPool& materialPool,
		Engine::TexturePool& texturePool,
		const std::shared_ptr<Engine::Mesh>& mesh,
		int index
	)
	{
		std::string matName = "mat_" + std::to_string(index);

		// 33% mart, 33% alien, 33% no texture
		int choice = Engine::RandInt(0, 2);
		std::shared_ptr<Engine::Texture2D> tex = nullptr;

		if (choice == 0)
		{
			tex = texturePool.GetTexture2DLazy("mart");
		}
		else if (choice == 1)
		{
			tex = texturePool.GetTexture2DLazy("alien");
		}

		return materialPool.RegisterMaterialBinding(matName, mesh, tex);
	}

	void MakeTonsOfRandomPositionedEntities(Engine::Scene* scene)
	{
		auto& registry = scene->GetRegistry();
		auto& meshPool = scene->GetMeshPool();
		auto& texturePool = scene->GetTexturePool();
		auto& materialPool = scene->GetMaterialPool();

		const int total = (GRID_HALF_SIZE * 2 + 1);

		// === Shared Assets Setup ===
		std::vector<std::shared_ptr<Engine::LegacyRenderBinding>> sharedBarrelMaterials;

		if constexpr (!fullyUniqueMeshes)
		{
			// Shared Cube
			auto cubeData = Engine::MakeCube();
			auto sharedCube = meshPool.RegisterMesh("SharedCube", cubeData.vertices, cubeData.indices);
			materialPool.RegisterMaterialBinding("RegularCube", sharedCube);
			materialPool.RegisterMaterialBinding("MartCube", sharedCube, texturePool.GetTexture2DLazy("mart"));
			materialPool.RegisterMaterialBinding("AlienCube", sharedCube, texturePool.GetTexture2DLazy("alien"));

			// Shared Sphere
			auto sphereData = Engine::MakeSphere(16, 32, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			auto sharedSphere = meshPool.RegisterMesh("SharedSphere", sphereData.vertices, sphereData.indices);
			materialPool.RegisterMaterialBinding("RegularSphere", sharedSphere);
			materialPool.RegisterMaterialBinding("MartSphere", sharedSphere, texturePool.GetTexture2DLazy("mart"));
			materialPool.RegisterMaterialBinding("AlienSphere", sharedSphere, texturePool.GetTexture2DLazy("alien"));

			// Shared Barrel (composite)
			sharedBarrelMaterials = materialPool.LoadAndRegisterCompositeMaterial("Assets/Models/barrel.glb");
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
						glm::vec3 euler = Engine::RandVec3(0.0f, 360.0f);
						glm::quat rot = glm::quat(glm::radians(euler));
						registry.emplace<Engine::Transform>(entity, pos, glm::vec3(1.0f), rot);
					}
					else
					{
						registry.emplace<Engine::Transform>(entity, pos, glm::vec3(1.0f));
					}

					// === Random Mesh Type: 0 = Sphere, 1 = Cube, 2 = Barrel ===
					int choice = Engine::RandInt(0, 2);

					if (choice == 0) // Sphere
					{
						if constexpr (fullyUniqueMeshes)
						{
							int latSegments = Engine::RandInt(8, 24);
							int longSegments = Engine::RandInt(16, 48);
							glm::vec3 top = Engine::RandVec3(0.2f, 1.0f);
							glm::vec3 mid = Engine::RandVec3(0.2f, 1.0f);
							glm::vec3 bot = Engine::RandVec3(0.2f, 1.0f);

							auto sphereData = Engine::MakeSphere(latSegments, longSegments, top, mid, bot);
							std::string name = "sphere_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
							auto mesh = meshPool.RegisterMesh(name, sphereData.vertices, sphereData.indices);
							auto material = RegisterRandomMaterial(materialPool, texturePool, mesh, Engine::RandInt(0, 999999));
							registry.emplace<Engine::Material>(entity, material);
						}
						else
						{
							auto mesh = meshPool.GetMesh("SharedSphere");
							int matID = Engine::RandInt(0, 2);
							std::string matName = (matID == 0) ? "RegularSphere" : (matID == 1) ? "MartSphere" : "AlienSphere";
							auto material = materialPool.GetMaterialBinding(matName);
							registry.emplace<Engine::Material>(entity, material);
						}
					}
					else if (choice == 1) // Cube
					{
						if constexpr (fullyUniqueMeshes)
						{
							auto cubeData = Engine::MakeRandomColoredCube();
							std::string name = "cube_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
							auto mesh = meshPool.RegisterMesh(name, cubeData.vertices, cubeData.indices);
							auto material = RegisterRandomMaterial(materialPool, texturePool, mesh, Engine::RandInt(0, 999999));
							registry.emplace<Engine::Material>(entity, material);
						}
						else
						{
							auto mesh = meshPool.GetMesh("SharedCube");
							int matID = Engine::RandInt(0, 2);
							std::string matName = (matID == 0) ? "RegularCube" : (matID == 1) ? "MartCube" : "AlienCube";
							auto material = materialPool.GetMaterialBinding(matName);
							registry.emplace<Engine::Material>(entity, material);
						}
					}
					else // Barrel (always shared)
					{
						registry.emplace<Engine::CompositeMaterial>(entity, Engine::CompositeMaterial(sharedBarrelMaterials, "Assets/Models/barrel.glb"));
						Engine::Transform& trans = registry.get<Engine::Transform>(entity);
						trans.SetScale(glm::vec3(0.2f));
					}

					// Optional spin behavior
					if constexpr (doRandomBehaviors)
					{
						if (Engine::RandInt(0, 1) == 0)
						{
							scene->EmplaceBehavior<Game::Spin>(entity, Engine::RandFloat(25.0f, 90.0f));
						}
					}
				}
			}
		}
	}

}
