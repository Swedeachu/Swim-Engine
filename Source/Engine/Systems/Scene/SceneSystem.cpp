#include "Engine/Systems/Scene/SceneSystem.h"

#include "Engine/Systems/Scene/SceneCommandBuffer.h"

#include <iostream>

namespace Engine
{
	void SceneSystem::SetServices(SceneServices value)
	{
		services = std::move(value);
		services.Behaviors = &behaviorRegistry;
		for (auto& [name, loaded] : scenes)
		{
			(void)name;
			InjectServices(*loaded.Instance);
		}
	}

	void SceneSystem::RegisterSceneType(std::string name, SceneFactory sceneFactory)
	{
		sceneCatalog.Register(std::move(name), std::move(sceneFactory));
	}

	void SceneSystem::AddLoaded(const std::string& name, std::shared_ptr<Scene> scene)
	{
		if (!scene)
		{
			throw std::invalid_argument("SceneSystem: scene '" + name + "' is null.");
		}
		if (services.HasCore())
		{
			InjectServices(*scene);
		}
		SceneId id(nextSceneId++);
		scenes.emplace(name, LoadedScene{ id, std::move(scene), false });
	}

	int SceneSystem::Awake()
	{
		if (!services.HasCore())
		{
			std::cerr << "[SceneSystem] Required engine services were not injected before Awake.\n";
			return -1;
		}

		int err = 0;
		// Construct runtime instances from this system's explicit catalog (no global
		// registration or static initialisation).
		for (const SceneCatalog::Descriptor& descriptor : sceneCatalog.GetDescriptors())
		{
			if (scenes.contains(descriptor.Name))
			{
				continue;
			}
			std::shared_ptr<Scene> scene = descriptor.Create(descriptor.Name);
			if (!scene)
			{
				std::cerr << "[SceneSystem] Scene factory '" << descriptor.Name << "' returned null.\n";
				err = err ? err : -1;
				continue;
			}
			AddLoaded(descriptor.Name, std::move(scene));
		}

		for (auto& [name, loaded] : scenes)
		{
			(void)name;
			InjectServices(*loaded.Instance);
		}

		if (startupSceneName.empty() && !scenes.empty())
		{
			startupSceneName = scenes.begin()->first;
		}
		if (!startupSceneName.empty() && !scenes.contains(startupSceneName))
		{
			std::cerr << "[SceneSystem] Startup scene '" << startupSceneName << "' is not registered.\n";
			err = err ? err : -1;
		}
		awake = true;
		return err;
	}

	int SceneSystem::Init()
	{
		if (startupSceneName.empty())
		{
			return 0;
		}
		auto it = scenes.find(startupSceneName);
		if (it == scenes.end())
		{
			return -1;
		}
		return ActivateLoaded(it->second, it->first);
	}

	int SceneSystem::ActivateLoaded(LoadedScene& loaded, const std::string& name)
	{
		activeScene = loaded.Instance;
		activeSceneId = loaded.Id;
		InjectServices(*activeScene);

		int err = 0;
		if (!loaded.Awakened)
		{
			loaded.Awakened = true;
			activeScene->InternalSceneAwake();
			if (const int result = activeScene->Awake(); result != 0)
			{
				std::cerr << "[SceneSystem] Scene '" << name << "' failed to Awake.\n";
				err = result;
			}
		}
		activeScene->InternalSceneInit();
		if (const int result = activeScene->Init(); result != 0)
		{
			std::cerr << "[SceneSystem] Scene '" << name << "' failed to Init.\n";
			err = err ? err : result;
		}
		activeScene->InternalScenePostInit();
		return err;
	}

	void SceneSystem::ExitActive()
	{
		if (!activeScene)
		{
			return;
		}
		if (activeScene->Exit() != 0)
		{
			std::cerr << "[SceneSystem] Scene '" << activeScene->GetName() << "' failed to Exit.\n";
		}
		activeScene->InternalSceneExit();
	}

	void SceneSystem::BeginFrame()
	{
		if (!pendingScene.empty())
		{
			std::string name = std::move(pendingScene);
			pendingScene.clear();
			pendingReload = false;
			SetScene(name);
		}
		else if (pendingReload)
		{
			pendingReload = false;
			ReloadActiveScene();
		}

		if (activeScene)
		{
			activeScene->BeginFrameTransformTracking();
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
		ExitActive();
		activeScene.reset();
		activeSceneId = {};
		scenes.clear();
		awake = false;
		return 0;
	}

	void SceneSystem::OnEngineStateChanged(EngineState previous, EngineState current)
	{
		if (activeScene)
		{
			activeScene->InternalStateChanged(previous, current);
		}
		if (current == EngineState::Stopped && previous != EngineState::None)
		{
			RequestReload();
		}
	}

	void SceneSystem::SetScene(const std::string& name)
	{
		auto it = scenes.find(name);
		if (it == scenes.end())
		{
			throw std::runtime_error("Scene with name '" + name + "' does not exist.");
		}
		ExitActive();
		ActivateLoaded(it->second, it->first);
	}

	void SceneSystem::ReloadActiveScene()
	{
		if (!activeScene)
		{
			return;
		}
		const std::string name = GetActiveSceneName();
		ExitActive();
		auto it = scenes.find(name);
		if (it != scenes.end())
		{
			ActivateLoaded(it->second, it->first);
		}
		++reloadCount;
	}

	std::string SceneSystem::GetActiveSceneName() const
	{
		for (const auto& [name, loaded] : scenes)
		{
			if (loaded.Instance == activeScene)
			{
				return name;
			}
		}
		return {};
	}

	SceneId SceneSystem::FindSceneId(std::string_view name) const
	{
		auto it = scenes.find(std::string(name));
		return it == scenes.end() ? SceneId{} : it->second.Id;
	}

	std::vector<std::string> SceneSystem::GetSceneNames() const
	{
		std::vector<std::string> names;
		names.reserve(scenes.size());
		for (const auto& [name, loaded] : scenes)
		{
			(void)loaded;
			names.push_back(name);
		}
		return names;
	}

	void SceneSystem::InjectServices(Scene& scene)
	{
		SceneServices injected = services;
		injected.Behaviors = &behaviorRegistry;
		scene.SetServices(std::move(injected));
	}
} // namespace Engine
