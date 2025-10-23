#include "PCH.h"
#include "SandBox.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"
#include "Engine\Components\CompositeMaterial.h"
#include "Engine\Components\MeshDecorator.h"
#include "Engine\Systems\Entity\EntityFactory.h"
#include "Game\Behaviors\CameraControl\EditorCamera.h"
#include "Game\Behaviors\Demo\SimpleMovement.h"
#include "Game\Behaviors\Demo\CubeMapControlTest.h"
#include "Game\Behaviors\Demo\Spin.h"
#include "Engine\Components\TextComponent.h"
#include "Engine\Systems\Renderer\Core\Font\FontPool.h"
#include "Game\Behaviors\CameraControl\RayCasterCameraControl.h"
#include "Engine\Systems\Renderer\Core\Meshes\PrimitiveMeshes.h"
#include "Game\Testing\PrimitiveTest.h"
#include "Game\Testing\MeshDrawingStressTest.h"
#include "Game\Testing\TextAndUiTest.h"
#include "Game\Behaviors\Demo\OrbitSystem.h"

namespace Game
{

	constexpr static bool doStressTest = false;
	constexpr static bool doUI = true;
	constexpr static bool glbTests = false;
	constexpr static bool doSponza = true; // glbTests must be true for this to happen!
	constexpr static bool testPrimitiveMeshes = true;
	constexpr static bool doWorldSpaceParentTesting = true; // via orbit system

	int SandBox::Awake()
	{
		std::cout << name << " Awoke" << std::endl;
		GetSceneSystem()->SetScene(name, true, false, false); // set ourselves to active first scene
		return 0;
	}

	int SandBox::Init()
	{
		std::cout << name << " Init" << std::endl;

		// Get the MeshPool instance
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();

		auto cubeData = Engine::MakeCube();
		auto rainBowQuad = Engine::MakeQuad
		(
			1, 1, 0, 0,
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f }
		);

		// Register both meshes
		auto mesh1 = meshPool.RegisterMesh("RainbowCube", cubeData.vertices, cubeData.indices);
		auto mesh2 = meshPool.RegisterMesh("RainbowQuad", rainBowQuad.vertices, rainBowQuad.indices);

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

		auto sphereMesh = meshPool.RegisterMesh("Sphere", sphereData.vertices, sphereData.indices);

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
		entityFactory.CreateWithBehaviors<EditorCamera, CubeMapControlTest/*, RayCasterCameraControl*/>();

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

		if constexpr (doWorldSpaceParentTesting) TestParenting(this, glm::vec3(0, 20, 0));

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
