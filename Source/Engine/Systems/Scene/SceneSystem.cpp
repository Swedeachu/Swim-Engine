#include "PCH.h"
#include "SceneSystem.h"
#include "Engine/SwimEngine.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/ObjectTag.h"

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

		// We want to then register all our editor commands
		RegisterEditorCommands();

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

	void SceneSystem::RegisterEditorCommands()
	{
		auto engine = SwimEngine::GetInstance();
		if (!engine)
		{
			return;
		}

		std::shared_ptr<CommandSystem> cmd = engine->GetCommandSystem();
		if (!cmd)
		{
			return;
		}

		std::weak_ptr<SceneSystem> self = shared_from_this();

		// (scene.entity.create parentId)
		// parentId == 0 -> no parent (root under scene)
		cmd->Register<unsigned int>(
			"scene.entity.create",
			std::function<void(unsigned int)>(
			[self](unsigned int parentId)
		{
			auto s = self.lock();
			if (!s)
			{
				return;
			}

			std::shared_ptr<Scene> scene = s->GetActiveScene();
			if (!scene)
			{
				return;
			}

			entt::entity e = scene->CreateEntity();

			// Give it a Transform so it becomes visible / parentable – this will also
			// trigger serialization hooks for "entity created".
			scene->EmplaceComponent<Transform>(e);

			entt::registry& reg = scene->GetRegistry();

			// Optional parenting
			if (parentId != 0u)
			{
				entt::entity parent = static_cast<entt::entity>(parentId);
				if (reg.valid(parent))
				{
					scene->SetParent(e, parent);
				}
			}

			// Give it a default ObjectTag name ("Entity 12" etc.)
			const std::string name = scene->GetEntityName(e);
			scene->SetTag(e, TagConstants::WORLD, name);

			// No need to manually ping SerializedSceneManager;
			// registry hooks will send created/updated events.
		}));

		// (scene.entity.destroy entityId destroyChildren)
		// destroyChildren: true = destroy subtree, false = keep children and detach them.
		cmd->Register<unsigned int, bool>(
			"scene.entity.destroy",
			std::function<void(unsigned int, bool)>(
			[self](unsigned int entityId, bool destroyChildren)
		{
			auto s = self.lock();
			if (!s)
			{
				return;
			}

			std::shared_ptr<Scene> scene = s->GetActiveScene();
			if (!scene)
			{
				return;
			}

			entt::entity e = static_cast<entt::entity>(entityId);
			entt::registry& reg = scene->GetRegistry();

			if (!reg.valid(e))
			{
				return;
			}

			// DestroyEntity already drives serialization via component hooks.
			scene->DestroyEntity(e, true, destroyChildren);
		}));

		// Helper: add a known component by string name
		auto addComponentByName = [](Scene& scene, entt::entity e, const std::string& componentName)
		{
			entt::registry& reg = scene.GetRegistry();

			if (!reg.valid(e))
			{
				return;
			}

			if (componentName == "Transform")
			{
				if (!reg.any_of<Transform>(e))
				{
					scene.EmplaceComponent<Transform>(e);
				}
			}
			else if (componentName == "Material")
			{
				if (!reg.any_of<Material>(e))
				{
					scene.EmplaceComponent<Material>(e);
				}
			}
			else if (componentName == "ObjectTag")
			{
				if (!reg.any_of<ObjectTag>(e))
				{
					const std::string name = scene.GetEntityName(e);
					scene.EmplaceComponent<ObjectTag>(e, TagConstants::WORLD, name);
				}
			}
			// else: could log unknown component
		};

		// Helper: remove a known component by string name
		auto removeComponentByName = [](Scene& scene, entt::entity e, const std::string& componentName)
		{
			entt::registry& reg = scene.GetRegistry();

			if (!reg.valid(e))
			{
				return;
			}

			if (componentName == "Transform")
			{
				// Removing Transform is effectively "delete from editor's POV".
				// You probably don't want to allow this via context menu directly;
				// prefer scene.entity.destroy instead. Here we just do nothing.
				return;
			}
			else if (componentName == "Material")
			{
				scene.RemoveComponent<Material>(e);
			}
			else if (componentName == "ObjectTag")
			{
				scene.RemoveTag(e);
			}
			// else: unknown component -> ignore
		};

		// (scene.entity.addComponent entityId "ComponentName")
		cmd->Register<unsigned int, std::string>(
			"scene.entity.addComponent",
			std::function<void(unsigned int, std::string)>(
			[self, addComponentByName](unsigned int entityId, std::string componentName)
		{
			auto s = self.lock();
			if (!s)
			{
				return;
			}

			std::shared_ptr<Scene> scene = s->GetActiveScene();
			if (!scene)
			{
				return;
			}

			entt::entity e = static_cast<entt::entity>(entityId);
			addComponentByName(*scene, e, componentName);
		}));

		// (scene.entity.removeComponent entityId "ComponentName")
		cmd->Register<unsigned int, std::string>(
			"scene.entity.removeComponent",
			std::function<void(unsigned int, std::string)>(
			[self, removeComponentByName](unsigned int entityId, std::string componentName)
		{
			auto s = self.lock();
			if (!s)
			{
				return;
			}

			std::shared_ptr<Scene> scene = s->GetActiveScene();
			if (!scene)
			{
				return;
			}

			entt::entity e = static_cast<entt::entity>(entityId);
			removeComponentByName(*scene, e, componentName);
		}));
	}

}
