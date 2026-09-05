#include "PCH.h"
#include "SceneSystem.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "SceneCommandBuffer.h"

namespace Engine
{

	void SceneSystem::RegisterSceneType(std::string name, SceneFactory sceneFactory)
	{
		sceneCatalog.Register(std::move(name), std::move(sceneFactory));
	}

	int SceneSystem::Awake()
	{
		if (!services.IsValid())
		{
			std::cerr << "[SceneSystem] Required engine services were not injected before Awake.\n";
			return -1;
		}

		int err = 0;

		// Construct runtime instances from this SceneSystem's explicit catalog.
		// There is no process-global scene registration or static initialization path.
		for (const SceneCatalog::Descriptor& descriptor : sceneCatalog.GetDescriptors())
		{
			if (scenes.contains(descriptor.Name))
			{
				std::cerr << "Scene instance '" << descriptor.Name << "' is already loaded.\n";
				if (err == 0)
				{
					err = -1;
				}
				continue;
			}

			std::shared_ptr<Scene> scene = descriptor.Create(descriptor.Name);
			if (!scene)
			{
				std::cerr << "Scene factory '" << descriptor.Name << "' returned null.\n";
				if (err == 0)
				{
					err = -1;
				}
				continue;
			}

			SceneId id(nextSceneId++);
			scenes.emplace(descriptor.Name, LoadedScene{ id, std::move(scene) });
		}

		// Inject services into every scene, including scenes registered explicitly
		// before Awake(), then awaken the scene only after all dependencies exist.
		for (auto& [name, loadedScene] : scenes)
		{
			std::shared_ptr<Scene>& scene = loadedScene.Instance;
			InjectServices(*scene);
			scene->InternalSceneAwake();
			int terr = scene->Awake();
			if (terr != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Awake.\n";
				if (err == 0) { err = terr; }
			}
		}

		if (!startupSceneName.empty())
		{
			auto startup = scenes.find(startupSceneName);
			if (startup == scenes.end())
			{
				std::cerr << "Startup scene '" << startupSceneName << "' is not registered.\n";
				if (err == 0)
				{
					err = -1;
				}
			}
			else
			{
				activeScene = startup->second.Instance;
				activeSceneId = startup->second.Id;
			}
		}

		// Legacy external-editor scene commands are intentionally not registered.
		// Future editor tooling is internal engine UI and operates on engine state directly.

		return err;
	}

	int SceneSystem::Init()
	{
		if (activeScene)
		{
			activeScene->InternalSceneInit();
			activeScene->Init();
			activeScene->InternalScenePostInit();
		}

		return 0;
	}

	void SceneSystem::BeginFrame()
	{
		for (auto& [name, loaded] : scenes)
		{
			(void)name;
			if (loaded.Instance)
			{
				loaded.Instance->BeginFrameTransformTracking();
			}
		}
	}

	void SceneSystem::Update(double dt)
	{
		if (activeScene)
		{
			activeScene->InternalSceneUpdate(dt);
			activeScene->Update(dt);
			activeScene->InternalScenePostUpdate(dt);
		}
	}

	void SceneSystem::FixedUpdate(unsigned int tickThisSecond)
	{
		if (activeScene)
		{
			activeScene->InternalFixedUpdate(tickThisSecond);
			activeScene->FixedUpdate(tickThisSecond);
			activeScene->InternalFixedPostUpdate(tickThisSecond);
		}
	}

	int SceneSystem::Exit()
	{
		int err = 0;

		for (auto& [name, loadedScene] : scenes)
		{
			std::shared_ptr<Scene>& scene = loadedScene.Instance;
			scene->InternalSceneExit();
			int sceneError = scene->Exit();
			if (sceneError != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Exit.\n";
				if (err == 0)
				{
					err = sceneError;
				}
			}
		}

		activeScene.reset();
		activeSceneId = {};
		scenes.clear();
		return err;
	}

	SceneId SceneSystem::FindSceneId(std::string_view name) const
	{
		auto it = scenes.find(std::string(name));
		if (it == scenes.end())
		{
			return {};
		}

		return it->second.Id;
	}

	void SceneSystem::SetScene(const std::string& name, bool exitCurrent, bool initNew, bool awakeNew)
	{
		// Check if the scene exists in the map
		auto it = scenes.find(name);
		if (it == scenes.end())
		{
			throw std::runtime_error("Scene with name '" + name + "' does not exist.");
		}

		// Exit the current scene if requested
		if (exitCurrent && activeScene)
		{
			activeScene->InternalSceneExit();
			if (activeScene->Exit() != 0)
			{
				std::cerr << "Failed to exit the current scene.\n";
			}
		}

		// Set the new active scene
		activeScene = it->second.Instance;
		activeSceneId = it->second.Id;
		if (activeScene)
		{
			InjectServices(*activeScene);
			if (awakeNew)
			{
				activeScene->InternalSceneAwake();
				if (activeScene->Awake() != 0)
				{
					std::cerr << "Failed to Awake the new scene '" << name << "'.\n";
				}
			}

			if (initNew)
			{
				activeScene->InternalSceneInit();
				if (activeScene->Init() != 0)
				{
					std::cerr << "Failed to Init the new scene '" << name << "'.\n";
				}
			}
		}
	}

	void SceneSystem::InjectServices(Scene& scene)
	{
		scene.SetInputSystem(services.Presentation.Input);
		scene.SetCameraSystem(services.Presentation.Camera);
		scene.SetEngineState(services.Core.State);
		scene.SetMeshPool(services.Presentation.Meshes);
		scene.SetTexturePool(services.Presentation.Textures);
		scene.SetMaterialPool(services.Presentation.Materials);
		scene.SetFontPool(services.Presentation.Fonts);
		scene.SetFileSystem(services.Core.Files);
		scene.SetJobSystem(services.Core.Jobs);
		scene.SetIoSystem(services.Core.IO);
		scene.SetAssetSystem(services.Core.Assets);
		scene.SetBehaviorRegistry(&behaviorRegistry);
		scene.SetFrameArena(services.Core.FrameMemory);
		scene.SetFPSProvider(services.Tools.GetFPS);
		scene.SetCommandDispatcher([commands = services.Tools.Commands](std::string_view command)
		{
			if (!commands)
			{
				return false;
			}

			return commands->ParseAndDispatch(command);
		});

		scene.SetCubeMapController(services.Presentation.CubeMap);
	}

	bool SceneSystem::DispatchCommand(std::string_view command)
	{
		if (!services.Tools.Commands)
		{
			return false;
		}

		return services.Tools.Commands->ParseAndDispatch(command);
	}

}
