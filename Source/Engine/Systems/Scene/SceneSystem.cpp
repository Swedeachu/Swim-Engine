#include "PCH.h"
#include "SceneSystem.h"

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
			}
		}
		factory.clear(); // do we want to clear?

		// Awake all registered scenes
		for (auto& [name, scene] : scenes)
		{
			if (scene->Awake() != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Awake.\n";
				return -1;
			}
		}

		return 0;
	}

	int SceneSystem::Init()
	{
		if (activeScene)
		{
			activeScene->Init();
		}

		return 0;
	}

	void SceneSystem::Update(double dt)
	{
		if (activeScene)
		{
			activeScene->Update(dt);
		}
	}

	void SceneSystem::FixedUpdate(unsigned int tickThisSecond)
	{
		if (activeScene)
		{
			activeScene->FixedUpdate(tickThisSecond);
		}
	}

	int SceneSystem::Exit()
	{
		for (auto& [name, scene] : scenes)
		{
			if (scene->Exit() != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Exit.\n";
				return -1;
			}
		}
		return 0;
	}

	void SceneSystem::SetScene(const std::string& name, bool exitCurrent)
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
			if (activeScene->Exit() != 0)
			{
				std::cerr << "Failed to exit the current scene.\n";
			}
		}

		// Set the new active scene
		activeScene = it->second;
		if (activeScene)
		{
			if (activeScene->Awake() != 0)
			{
				std::cerr << "Failed to Awake the new scene '" << name << "'.\n";
			}
			if (activeScene->Init() != 0)
			{
				std::cerr << "Failed to Init the new scene '" << name << "'.\n";
			}
		}
	}

}
