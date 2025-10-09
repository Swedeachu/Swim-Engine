#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"
#include "Engine\Components\CompositeMaterial.h"
#include "Engine\Components\MeshDecorator.h"
#include "RandomUtils.h"
#include "Engine\Systems\Entity\EntityFactory.h"
#include "Game\Behaviors\CameraControl\EditorCamera.h"
#include "Game\Behaviors\Demo\SimpleMovement.h"
#include "Game\Behaviors\Demo\CubeMapControlTest.h"
#include "Game\Behaviors\Demo\Spin.h"
#include "Game\Behaviors\Demo\MouseInputDemoBehavior.h"
#include "Engine\Components\TextComponent.h"
#include "Engine\Systems\Renderer\Core\Font\FontPool.h"
#include "Engine\Systems\Renderer\Core\Meshes\PrimitiveMeshes.h"
#include "Game\Behaviors\CameraControl\RayCasterCameraControl.h"
#include "Game\Behaviors\Demo\SetTextCallBack.h"

namespace Game
{

	constexpr static bool doStressTest = false;
	constexpr static bool doUI = true;
	constexpr static bool doTextUI = true;
	constexpr static bool doButtonUI = false;
	constexpr static bool glbTests = true;
	constexpr static bool doSponza = true; // glbTests must be true for this to happen!

	constexpr static bool testPrimitiveMeshes = true;
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
			auto cubeData = Engine::MakeCube();
			auto sharedCube = meshPool.RegisterMesh("SharedCube", cubeData.first, cubeData.second);
			materialPool.RegisterMaterialData("RegularCube", sharedCube);
			materialPool.RegisterMaterialData("MartCube", sharedCube, texturePool.GetTexture2DLazy("mart"));
			materialPool.RegisterMaterialData("AlienCube", sharedCube, texturePool.GetTexture2DLazy("alien"));

			// Shared Sphere
			auto sphereData = Engine::MakeSphere(16, 32, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
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

							auto sphereData = Engine::MakeSphere(latSegments, longSegments, top, mid, bot);
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
							auto cubeData = Engine::MakeRandomColoredCube();
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
							scene->EmplaceBehavior<Game::Spin>(entity, Engine::randFloat(25.0f, 90.0f));
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

		if (doButtonUI)
		{
			auto whiteEntity = scene->CreateEntity();

			// Place it in the screen
			glm::vec3 whiteEntityScreenPos = glm::vec3(300, 900, 0.0f);

			// Pixel size
			glm::vec3 whiteEntitySize = glm::vec3(300.0f, 150.0f, 1.0f);

			scene->AddComponent<Engine::Transform>(whiteEntity, Engine::Transform(whiteEntityScreenPos, whiteEntitySize, glm::quat(), Engine::TransformSpace::Screen));
			scene->AddComponent<Engine::Material>(whiteEntity, Engine::Material(whiteMaterial));
			scene->EmplaceBehavior<MouseInputDemoBehavior>(whiteEntity); // with a behavior to demonstrate mouse input callbacks

			Engine::MeshDecorator decorator = Engine::MeshDecorator(
				glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),    // fill
				glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),    // stroke
				glm::vec2(16.0f, 16.0f),              // stroke width X/Y
				glm::vec2(32.0f, 32.0f),              // corner radius X/Y
				glm::vec2(4.0f),                      // padding
				true, true, true, true                // to enable: rounded, stroke, fill, material texture
			);

			scene->AddComponent<Engine::MeshDecorator>(whiteEntity, decorator);
		}

		if (doTextUI)
		{
			// Text entity
			auto textEntity = scene->CreateEntity();
			glm::vec3 textEntityScreenPos = glm::vec3(960, 1020, 0.0f);
			glm::vec3 textEntitySize = glm::vec3(50.0f, 50.0f, 1.0f);
			scene->AddComponent<Engine::Transform>(textEntity, Engine::Transform(textEntityScreenPos, textEntitySize, glm::quat(), Engine::TransformSpace::Screen));

			// Get the font pool and the roboto_bold font from it 
			Engine::FontPool& fontPool = Engine::FontPool::GetInstance();
			std::shared_ptr<Engine::FontInfo> roboto = fontPool.GetFontInfo("roboto_bold");

			// Create a text component which uses the roboto_font
			Engine::TextComponent textComponent = Engine::TextComponent();
			textComponent.fillColor = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
			textComponent.strokeColor = glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
			textComponent.strokeWidth = 2.0f;
			textComponent.SetAlignment(Engine::TextAllignemt::Center);
			textComponent.SetText("Swim Engine");
			textComponent.SetFont(roboto);

			scene->AddComponent<Engine::TextComponent>(textEntity, textComponent);

			// fps counter
			auto fpsEntity = scene->CreateEntity();
			glm::vec3 fpsEntityScreenPos = glm::vec3(1700, 1020, 0.0f);
			glm::vec3 fpsEntitySize = glm::vec3(50.0f, 50.0f, 1.0f);
			scene->AddComponent<Engine::Transform>(fpsEntity, Engine::Transform(fpsEntityScreenPos, fpsEntitySize, glm::quat(), Engine::TransformSpace::Screen));

			Engine::TextComponent fpsText = Engine::TextComponent();
			fpsText.fillColor = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
			fpsText.strokeColor = glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
			fpsText.strokeWidth = 2.0f;
			textComponent.SetAlignment(Engine::TextAllignemt::Left);
			fpsText.SetText("FPS: ");
			fpsText.SetFont(roboto);

			scene->AddComponent<Engine::TextComponent>(fpsEntity, textComponent);

			Game::SetTextCallback* fpsBehavior = scene->EmplaceBehavior<Game::SetTextCallback>(fpsEntity, /*chroma*/ true);
			fpsBehavior->SetCallback([engine](Engine::TextComponent& tc, entt::entity e, double)
			{
				int fps = engine->GetFPS();
				const std::string s = "FPS: " + std::to_string(fps);
				tc.SetText(s);
			});

			// camera coords
			auto coordEntity = scene->CreateEntity();
			glm::vec3 coordEntityScreenPos = glm::vec3(20, 1020, 0.0f);
			glm::vec3 coordEntitySize = glm::vec3(50.0f, 50.0f, 1.0f);
			scene->AddComponent<Engine::Transform>(coordEntity, Engine::Transform(coordEntityScreenPos, coordEntitySize, glm::quat(), Engine::TransformSpace::Screen));

			Engine::TextComponent coordTextComponent = Engine::TextComponent();
			coordTextComponent.fillColor = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
			coordTextComponent.strokeColor = glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
			coordTextComponent.strokeWidth = 2.0f;
			coordTextComponent.SetAlignment(Engine::TextAllignemt::Left);
			coordTextComponent.SetText("0, 0, 0");
			coordTextComponent.SetFont(roboto);

			scene->AddComponent<Engine::TextComponent>(coordEntity, coordTextComponent);
			Game::SetTextCallback* coordBehavior = scene->EmplaceBehavior<Game::SetTextCallback>(coordEntity, /*chroma*/ true);
			coordBehavior->SetCallback([cameraSystem = scene->GetCameraSystem()](Engine::TextComponent& tc, entt::entity e, double)
			{
				Engine::Camera cam = cameraSystem->GetCamera();
				const glm::vec3 p = cam.GetPosition();
				const glm::vec3 r = cam.GetRotationEuler();
				const std::string newText = (
					ChromaHelper::strf(p.x) + ", " + ChromaHelper::strf(p.y) + ", " + ChromaHelper::strf(p.z)
					+ "\n" +
					ChromaHelper::strf(r.x) + ", " + ChromaHelper::strf(r.y) + ", " + ChromaHelper::strf(r.z)
					);
				tc.SetText(newText);
			});
		}

		// Below here is a bunch of bools for messing with making a second UI entity to test stuff out with

		constexpr bool makeSecondEntity = true;
		if constexpr (!makeSecondEntity || !doButtonUI) return;

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
				auto [circleVertices, circleIndices] = Engine::GenerateCircleMesh(1.0f, 128, { 1.0f, 0.0f, 0.0f });
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

	void TestPrimitives(Engine::Scene* scene)
	{
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();

		// ===== Helpers =====
		auto makeSolidSphere = [&](const std::string& name, const glm::vec3& c, int lat = 24, int lon = 48)
		{
			auto data = Engine::MakeSphere(lat, lon, c, c, c);
			auto mesh = meshPool.RegisterMesh(name, data.first, data.second);
			return materialPool.RegisterMaterialData(name + "_mat", mesh);
		};

		auto makeSolidCylinder = [&](const std::string& name, const glm::vec3& c, float r = 0.25f, float h = 1.0f, uint32_t seg = 64) // skinnier r=0.25
		{
			auto data = Engine::MakeCylinder(r, h, seg, c);
			auto mesh = meshPool.RegisterMesh(name, data.first, data.second);
			return materialPool.RegisterMaterialData(name + "_mat", mesh);
		};

		auto makeSolidCone = [&](const std::string& name, const glm::vec3& c, float r = 0.5f, float h = 1.0f, uint32_t seg = 64)
		{
			auto data = Engine::MakeCone(r, h, seg, c);
			auto mesh = meshPool.RegisterMesh(name, data.first, data.second);
			return materialPool.RegisterMaterialData(name + "_mat", mesh);
		};

		auto makeSolidTorus = [&](const std::string& name, const glm::vec3& c, float outerR, float thickness, uint32_t segU = 48, uint32_t segV = 24)
		{
			auto data = Engine::MakeTorus(outerR, thickness, segU, segV, c);
			auto mesh = meshPool.RegisterMesh(name, data.first, data.second);
			return materialPool.RegisterMaterialData(name + "_mat", mesh);
		};

		const glm::vec3 RED = { 1.0f, 0.0f, 0.0f };
		const glm::vec3 GREEN = { 0.0f, 1.0f, 0.0f };
		const glm::vec3 BLUE = { 0.0f, 0.0f, 1.0f };

		// Left-shifted layout
		const float startX = -20.0f; // everything off to the left
		const float stepX = 3.0f;  // horizontal spacing
		const float yCenter = 0.5f;

		// Rows (front-to-back)
		const float z_spheres = -9.0f;
		const float z_cones = -3.0f;
		const float z_cylinders = 3.0f;
		const float z_torus_thin = 9.0f;
		const float z_torus_medium = 12.0f;
		const float z_torus_fat = 15.0f;

		// ===== Spheres (R,G,B) =====
		auto sphRed = makeSolidSphere("Prim_Sphere_Red", RED);
		auto sphGreen = makeSolidSphere("Prim_Sphere_Green", GREEN);
		auto sphBlue = makeSolidSphere("Prim_Sphere_Blue", BLUE);

		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 0 * stepX, yCenter, z_spheres), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(sphRed));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 1 * stepX, yCenter, z_spheres), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(sphGreen));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 2 * stepX, yCenter, z_spheres), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(sphBlue));
		}

		// ===== Cones (R,G,B) =====
		auto coneRed = makeSolidCone("Prim_Cone_Red", RED);
		auto coneGreen = makeSolidCone("Prim_Cone_Green", GREEN);
		auto coneBlue = makeSolidCone("Prim_Cone_Blue", BLUE);

		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 0 * stepX, yCenter, z_cones), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(coneRed));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 1 * stepX, yCenter, z_cones), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(coneGreen));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 2 * stepX, yCenter, z_cones), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(coneBlue));
		}

		// ===== Cylinders (R,G,B) — skinnier radius =====
		auto cylRed = makeSolidCylinder("Prim_Cyl_Red", RED, 0.25f);
		auto cylGreen = makeSolidCylinder("Prim_Cyl_Green", GREEN, 0.25f);
		auto cylBlue = makeSolidCylinder("Prim_Cyl_Blue", BLUE, 0.25f);

		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 0 * stepX, yCenter, z_cylinders), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(cylRed));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 1 * stepX, yCenter, z_cylinders), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(cylGreen));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 2 * stepX, yCenter, z_cylinders), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(cylBlue));
		}

		// ===== Toruses (Skinny, Medium, Fat) — each with R,G,B =====
		const float torusOuter = 0.40f;           // main ring radius
		const float torusThin = 0.05f;
		const float torusMed = 0.12f;
		const float torusFat = 0.20f;

		// Skinny
		auto torThinR = makeSolidTorus("Prim_Torus_Thin_R", RED, torusOuter, torusThin);
		auto torThinG = makeSolidTorus("Prim_Torus_Thin_G", GREEN, torusOuter, torusThin);
		auto torThinB = makeSolidTorus("Prim_Torus_Thin_B", BLUE, torusOuter, torusThin);

		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 0 * stepX, yCenter, z_torus_thin), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torThinR));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 1 * stepX, yCenter, z_torus_thin), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torThinG));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 2 * stepX, yCenter, z_torus_thin), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torThinB));
		}

		// Medium
		auto torMedR = makeSolidTorus("Prim_Torus_Med_R", RED, torusOuter, torusMed);
		auto torMedG = makeSolidTorus("Prim_Torus_Med_G", GREEN, torusOuter, torusMed);
		auto torMedB = makeSolidTorus("Prim_Torus_Med_B", BLUE, torusOuter, torusMed);

		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 0 * stepX, yCenter, z_torus_medium), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torMedR));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 1 * stepX, yCenter, z_torus_medium), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torMedG));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 2 * stepX, yCenter, z_torus_medium), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torMedB));
		}

		// Fat
		auto torFatR = makeSolidTorus("Prim_Torus_Fat_R", RED, torusOuter, torusFat);
		auto torFatG = makeSolidTorus("Prim_Torus_Fat_G", GREEN, torusOuter, torusFat);
		auto torFatB = makeSolidTorus("Prim_Torus_Fat_B", BLUE, torusOuter, torusFat);

		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 0 * stepX, yCenter, z_torus_fat), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torFatR));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 1 * stepX, yCenter, z_torus_fat), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torFatG));
		}
		{
			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(glm::vec3(startX + 2 * stepX, yCenter, z_torus_fat), glm::vec3(1.0f)));
			scene->AddComponent<Engine::Material>(e, Engine::Material(torFatB));
		}

		// ===== Darker gray flat plane (cube mesh squashed on Y) =====
		{
			auto cubeData = Engine::MakeCube();

			std::vector<Engine::Vertex> darkVerts = cubeData.first;
			const glm::vec3 DARK_GREY(0.2f, 0.2f, 0.2f);
			for (auto& v : darkVerts) v.color = DARK_GREY;

			auto planeMesh = meshPool.RegisterMesh("Prim_DarkGreyPlane", darkVerts, cubeData.second);
			auto planeMat = materialPool.RegisterMaterialData("Prim_DarkGreyPlane_Mat", planeMesh);

			const glm::vec3 planeScale(2.0f, 0.02f, 2.0f);
			const glm::vec3 planePos(-20.0f, 0.0f, 0.0f);

			auto e = scene->CreateEntity();
			scene->AddComponent<Engine::Transform>(e, Engine::Transform(planePos, planeScale));
			scene->AddComponent<Engine::Material>(e, Engine::Material(planeMat));
		}
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
		auto cubeData = Engine::MakeCube();

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

		auto sphereData = Engine::MakeSphere(
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
		EmplaceBehavior<Spin>(billboard);
		//*/

		// World space text entity
		auto textEntity = CreateEntity();
		glm::vec3 textEntityScreenPos = glm::vec3(10.0f, 0.f, 0.0f);
		glm::vec3 textEntitySize = glm::vec3(1.0f, 1.0f, 1.0f);
		AddComponent<Engine::Transform>(textEntity, Engine::Transform(textEntityScreenPos, textEntitySize, glm::quat(), Engine::TransformSpace::World));

		// Get the font pool and the roboto_bold font from it 
		Engine::FontPool& fontPool = Engine::FontPool::GetInstance();
		std::shared_ptr<Engine::FontInfo> roboto = fontPool.GetFontInfo("roboto_bold");

		// Create a text component which uses the roboto_font
		Engine::TextComponent textComponent = Engine::TextComponent();
		textComponent.fillColor = glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f };
		textComponent.strokeColor = glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
		textComponent.strokeWidth = 2.0f;
		textComponent.SetText("World Space Text");
		textComponent.SetFont(roboto);

		AddComponent<Engine::TextComponent>(textEntity, textComponent);

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
		EmplaceBehavior<Game::Spin>(spinEntity, 90.0f); // 90 degrees per second

		// We can make the Movement entity like this (actual physical entity we can control with WASD simple controller)
		entityFactory.CreateWithTransformMaterialAndBehaviors<SimpleMovement>(
			Engine::Transform(glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(1.0f)),
			Engine::Material(materialData1)
		);

		// We can load scene scripts this way as a cool hack/trick
		// Makes an empty entity in the scene with these scripts on it (we can do this with as many behaviors as we want)
		entityFactory.CreateWithBehaviors<EditorCamera, CubeMapControlTest, RayCasterCameraControl>();

		if constexpr (doUI) MakeUI(this);

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

		// if we have any test model to load we can just do it here 
		/* TODO: check if asset exists on disk
		auto testData = Engine::CompositeMaterial(materialPool.LazyLoadAndGetCompositeMaterial("Assets/Models/test.glb"));
		if (!testData.subMaterials.empty())
		{
			auto test = CreateEntity();
			auto testTransform = AddComponent<Engine::Transform>(test, Engine::Transform(glm::vec3(6.0f, 0.0f, 0.0f), glm::vec3(1)));
			testTransform.SetRotationEuler(0, 0, 90);
			// EmplaceBehavior<Game::Spin>(test, 90.0f);
			AddComponent<Engine::CompositeMaterial>(test, Engine::CompositeMaterial(testData));
		}
		*/

		std::cout << "[Scene] Textures before GLB load: " << textureCountBefore << " | After: " << textureCountAfter << std::endl;

		// The real stress test
		if constexpr (doStressTest) MakeTonsOfRandomPositionedEntities(this);

		if constexpr (testPrimitiveMeshes) TestPrimitives(this);

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
