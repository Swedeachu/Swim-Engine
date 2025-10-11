#include "PCH.h"
#include "TextAndUiTest.h"
#include "Engine\Systems\Renderer\Core\Meshes\MeshPool.h"
#include "Engine\Systems\Renderer\Core\Textures\TexturePool.h"
#include "Engine\Systems\Renderer\Core\Material\MaterialPool.h"
#include "Engine\Systems\Renderer\Core\Meshes\PrimitiveMeshes.h"
#include "Engine\Components\Transform.h"
#include "Engine\Components\Material.h"
#include "Engine\Components\MeshDecorator.h"
#include "Engine\Components\TextComponent.h"
#include "Engine\Systems\Renderer\Core\Font\FontPool.h"
#include "Game\Behaviors\Demo\MouseInputDemoBehavior.h"
#include "Game\Behaviors\Demo\SetTextCallBack.h"

namespace Game
{

	constexpr static bool doTextUI = true;
	constexpr static bool doButtonUI = false;

	void MakeUI(Engine::Scene* scene)
	{
		// Get the MeshPool instance
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();
		auto& texturePool = Engine::TexturePool::GetInstance();
		auto engine = Engine::SwimEngine::GetInstance();

		// Define vertices and indices for a white quad mesh
		auto quadData = Engine::MakeQuad();

		auto whiteQuad = meshPool.RegisterMesh("WhiteQuad", quadData.vertices, quadData.indices);
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
				auto circleData = Engine::MakeCircle(1.0f, 128, { 1.0f, 0.0f, 0.0f });
				secondMesh = meshPool.RegisterMesh("SecondTestMeshUI", circleData.vertices, circleData.indices);
			}
			else
			{
				auto redQuad = Engine::MakeQuad(
					1, 1, 0, 0,
					{ 1.0f, 0.0f, 0.0f },
					{ 1.0f, 0.0f, 0.0f },
					{ 1.0f, 0.0f, 1.0f },
					{ 1.0f, 0.0f, 1.0f }
				);

				secondMesh = meshPool.RegisterMesh("SecondTestMeshUI", redQuad.vertices, redQuad.indices);
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

}
