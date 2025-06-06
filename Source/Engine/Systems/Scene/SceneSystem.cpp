#include "PCH.h"
#include "SceneSystem.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

	std::vector<std::shared_ptr<Engine::Scene>> Engine::SceneSystem::factory;

	void SceneSystem::Preregister(std::shared_ptr<Scene> scene)
	{
		factory.push_back(scene);
	}

	int SceneSystem::Awake()
	{
		// Register scenes from the factory into the map
		for (const auto& scene : factory)
		{
			if (scene)
			{
				scenes[scene->GetName()] = scene;
				scene->SetSceneSystem(shared_from_this());
				auto instance = SwimEngine::GetInstance();
				scene->SetInputManager(instance->GetInputManager());
				scene->SetCameraSystem(instance->GetCameraSystem());

				if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
				{
					scene->SetVulkanRenderer(instance->GetVulkanRenderer());
				}
				else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
				{
					scene->SetOpenGLRenderer(instance->GetOpenGLRenderer());
				}
			}
		}

		factory.clear(); // then do we want to clear?

		int err = 0;

		// Awake all registered scenes
		for (auto& [name, scene] : scenes)
		{
			scene->InternalSceneAwake();
			int terr = scene->Awake();
			if (terr != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Awake.\n";
				if (err == 0) err = terr;
			}
		}

		return err;
	}

	int SceneSystem::Init()
	{
		if (activeScene)
		{
			activeScene->InternalSceneInit();
			activeScene->Init();
			// no internal post init yet required yet, might need that sooner or later
		}

		return 0;
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

		for (auto& [name, scene] : scenes)
		{
			scene->InternalSceneExit();
			int terr = scene->Exit();
			if (terr != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Exit.\n";
				if (err == 0) err = terr;
			}
		}

		return err;
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
		activeScene = it->second;
		if (activeScene)
		{
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

}
