#include "PCH.h"
#include "SceneSystem.h"
#include "Engine/SwimEngine.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"

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
				if (err == 0) { err = terr; }
			}
		}

		// We want to then register all our editor commands
		if constexpr (SwimEngine::DefaultEngineState == EngineState::Editing)
		{
			RegisterEditorCommands();
			SendBehaviorsToEditor();
		}

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
				if (err == 0) { err = terr; }
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

	// Small helpers used by the add/remove component commands:

	void SceneSystem::AddComponentByName(Scene& scene, unsigned int entityId, const std::string& componentName)
	{
		entt::registry& reg = scene.GetRegistry();
		entt::entity e = static_cast<entt::entity>(entityId);

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
		else
		{
			std::cout << "SceneSystem::AddComponentByName | Unknown component: " << componentName << std::endl;
		}
	}

	void SceneSystem::RemoveComponentByName(Scene& scene, unsigned int entityId, const std::string& componentName)
	{
		entt::registry& reg = scene.GetRegistry();
		entt::entity e = static_cast<entt::entity>(entityId);

		if (!reg.valid(e))
		{
			return;
		}

		if (componentName == "Transform")
		{
			scene.RemoveComponent<Transform>(e);
		}
		else if (componentName == "Material")
		{
			scene.RemoveComponent<Material>(e);
			scene.RemoveComponent<CompositeMaterial>(e);
		}
		else if (componentName == "ObjectTag")
		{
			scene.RemoveTag(e);
		}
		// else: unknown component -> ignore
	}

	void SceneSystem::SendBehaviorsToEditor()
	{
		auto engine = SwimEngine::GetInstance();
		auto behaviorFactories = BehaviorFactory::GetInstance().GetFactories();
		for (const auto factory : behaviorFactories)
		{
			engine->SendEditorMessage("loadBehavior " + factory.first, /*channel:*/1);
		}
	}

	// Command registration entry point
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

		// Each of these is now a small, focused function
		RegisterEntityCreateCommand(cmd);
		RegisterEntityDestroyCommand(cmd);
		RegisterEntityAddComponentCommand(cmd);
		RegisterEntityRemoveComponentCommand(cmd);
		RegisterEntitySetMaterialCommand(cmd);
		RegisterEntityBehaviorAddCommand(cmd);
		RegisterEntityBehaviorRemoveCommand(cmd);
	}

	// (scene.entity.create parentId)
	// parentId == 0 -> no parent (root under scene)
	void SceneSystem::RegisterEntityCreateCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

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

			// Give it a Transform so it becomes visible / parentable  this will also
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
		}));
	}

	// (scene.entity.destroy entityId destroyChildren)
	// destroyChildren: true = destroy subtree, false = keep children and detach them.
	void SceneSystem::RegisterEntityDestroyCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

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
	}

	// (scene.entity.addComponent entityId "ComponentName")
	void SceneSystem::RegisterEntityAddComponentCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

		cmd->Register<unsigned int, std::string>(
			"scene.entity.addComponent",
			std::function<void(unsigned int, std::string)>(
			[self, this](unsigned int entityId, std::string componentName)
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

			AddComponentByName(*scene, entityId, componentName);
		}));
	}

	// (scene.entity.removeComponent entityId "ComponentName")
	void SceneSystem::RegisterEntityRemoveComponentCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

		cmd->Register<unsigned int, std::string>(
			"scene.entity.removeComponent",
			std::function<void(unsigned int, std::string)>(
			[self, this](unsigned int entityId, std::string componentName)
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

			RemoveComponentByName(*scene, entityId, componentName);
		}));
	}

	// (scene.entity.setMaterial entityId "MaterialKey")
	void SceneSystem::RegisterEntitySetMaterialCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

		cmd->Register<unsigned int, std::string>(
			"scene.entity.setMaterial",
			std::function<void(unsigned int, std::string)>(
			[self](unsigned int entityId, std::string materialKey)
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

			MaterialPool& materialPool = MaterialPool::GetInstance();

			// Check if this is a composite material
			if (materialPool.CompositeMaterialExists(materialKey))
			{
				try
				{
					// Attempt to get it
					auto data = materialPool.GetCompositeMaterialData(materialKey);
					// Remove since we are doing a material replacement
					if (reg.any_of<Material>(e)) { reg.remove<Material>(e); }
					if (reg.any_of<CompositeMaterial>(e)) { reg.remove<CompositeMaterial>(e); }
					// Replace in new one
					// std::cout << "Applying new composite material " << materialKey << std::endl;
					reg.emplace<CompositeMaterial>(e, data, materialKey);
					// Hack fix because bvh will not update while in editor mode sometimes
					auto bvh = scene->GetSceneBVH();
					if (bvh) { bvh->ForceUpdateNextFrame(); }
					// Done
					return;
				}
				catch (std::exception e)
				{
					std::cout << e.what() << std::endl;
					return;
				}
			}

			// Check if this is a regular single material
			if (materialPool.MaterialExists(materialKey))
			{
				try
				{
					// Attempt to get it
					auto data = materialPool.GetMaterialData(materialKey);
					// Remove since we are doing a material replacement
					if (reg.any_of<Material>(e)) { reg.remove<Material>(e); }
					if (reg.any_of<CompositeMaterial>(e)) { reg.remove<CompositeMaterial>(e); }
					// Replace in new one
					// std::cout << "Applying new material " << materialKey << std::endl;
					reg.emplace<Material>(e, data);
					// Hack fix because bvh will not update while in editor mode sometimes
					auto bvh = scene->GetSceneBVH();
					if (bvh) { bvh->ForceUpdateNextFrame(); }
					// Done
					return;
				}
				catch (std::exception e)
				{
					std::cout << e.what() << std::endl;
					return;
				}
			}

			std::cout << "Failed to apply material " << materialKey << std::endl;
		}));
	}

	void SceneSystem::RegisterEntityBehaviorAddCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

		cmd->Register<unsigned int, std::string>(
			"scene.entity.addBehavior",
			std::function<void(unsigned int, std::string)>(
			[self](unsigned int entityId, std::string behaviorName)
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

			// Try to attach the behavior by name
			Behavior* behavior = scene->EmplaceBehaviorByName(e, behaviorName);
			if (behavior == nullptr)
			{
				std::cout << "SceneSystem::RegisterEntityBehaviorAddCommand | Failed to add behavior: " << behaviorName << " to entity " << entityId << std::endl;
				return;
			}
		}));
	}

	void SceneSystem::RegisterEntityBehaviorRemoveCommand(std::shared_ptr<CommandSystem>& cmd)
	{
		std::weak_ptr<SceneSystem> self = shared_from_this();

		cmd->Register<unsigned int, std::string>(
			"scene.entity.removeBehavior",
			std::function<void(unsigned int, std::string)>(
			[self](unsigned int entityId, std::string behaviorName)
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

			/* TODO: not a method yet, behavior needs a get behavior name method of some sort
			// Try to remove the behavior by name
			const bool removed = scene->RemoveBehaviorByName(e, behaviorName);
			if (!removed)
			{
				std::cout << "SceneSystem::RegisterEntityBehaviorRemoveCommand | Failed to remove behavior: " << behaviorName << " from entity " << entityId << std::endl;
				return;
			}
			*/

			// Behaviors on this entity changed; refresh their caches
			scene->RefreshBehaviorFieldCacheForEntity(e);
		}));
	}


}
