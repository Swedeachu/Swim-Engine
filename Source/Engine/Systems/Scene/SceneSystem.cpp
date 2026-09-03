#include "PCH.h"
#include "SceneSystem.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"

namespace Engine
{

	std::vector<Engine::SceneSystem::PreregisteredScene> Engine::SceneSystem::factory;

	void SceneSystem::Preregister(std::string name, SceneFactory sceneFactory)
	{
		factory.push_back({ std::move(name), std::move(sceneFactory) });
	}

	int SceneSystem::Awake()
	{
		if (!services.IsValid())
		{
			std::cerr << "[SceneSystem] Required engine services were not injected before Awake.\n";
			return -1;
		}

		// Register fresh runtime scenes from static constructor descriptors.
		// The descriptors intentionally remain available so a second engine instance
		// in the same process receives the same preregistered scene types.
		for (const auto& descriptor : factory)
		{
			if (descriptor.Factory)
			{
				scenes[descriptor.Name] = descriptor.Factory();
			}
		}

		int err = 0;

		// Inject services into every scene, including scenes registered explicitly
		// before Awake(), then awaken the scene only after all dependencies exist.
		for (auto& [name, scene] : scenes)
		{
			InjectServices(*scene);
			scene->InternalSceneAwake();
			int terr = scene->Awake();
			if (terr != 0)
			{
				std::cerr << "Scene '" << name << "' failed to Awake.\n";
				if (err == 0) { err = terr; }
			}
		}

		// Register editor-only command surfaces from the actual runtime state.
		if (HasAnyEngineStates(*services.State, EngineState::Editing))
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
		scenes.clear();
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
		scene.SetSceneSystem(this);
		scene.SetInputManager(services.Input);
		scene.SetCameraSystem(services.Camera);
		scene.SetEngineState(services.State);
		scene.SetClipSpaceDepthRange(services.ClipDepth);
		scene.SetMeshPool(services.Meshes);
		scene.SetTexturePool(services.Textures);
		scene.SetMaterialPool(services.Materials);
		scene.SetFontPool(services.Fonts);
		scene.SetFileSystem(services.Files);
		scene.SetJobSystem(services.Jobs);
		scene.SetIoSystem(services.IO);
		scene.SetAssetSystem(services.Assets);
		scene.SetFrameArena(services.FrameMemory);
		scene.SetFPSProvider(services.GetFPS);

		if (services.Vulkan)
		{
			scene.SetVulkanRenderer(services.Vulkan);
		}
		else if (services.OpenGL)
		{
			scene.SetOpenGLRenderer(services.OpenGL);
		}
	}

	bool SceneSystem::DispatchCommand(std::string_view command)
	{
		if (!services.Commands)
		{
			return false;
		}

		const std::string ownedCommand(command);
		const bool ok = services.Commands->ParseAndDispatch(ownedCommand);

		if (services.SendEditorMessage)
		{
			services.SendEditorMessage(std::string(ok ? "(Recv [200]): " : "(Recv [400]): ") + ownedCommand, 1);
		}

		return ok;
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
		if (!services.SendEditorMessage)
		{
			return;
		}

		auto behaviorFactories = BehaviorFactory::GetInstance().GetFactories();
		for (const auto& factory : behaviorFactories)
		{
			services.SendEditorMessage("loadBehavior " + factory.first, /*channel:*/1);
		}
	}

	// Command registration entry point
	void SceneSystem::RegisterEditorCommands()
	{
		CommandSystem* cmd = services.Commands;
		if (!cmd)
		{
			return;
		}

		// Each of these is now a small, focused function. CommandSystem outlives
		// SceneSystem in the engine's explicit shutdown order, so these callbacks
		// may safely keep a non-owning SceneSystem pointer.
		RegisterEntityCreateCommand(*cmd);
		RegisterEntityDestroyCommand(*cmd);
		RegisterEntityAddComponentCommand(*cmd);
		RegisterEntityRemoveComponentCommand(*cmd);
		RegisterEntitySetMaterialCommand(*cmd);
		RegisterEntityBehaviorAddCommand(*cmd);
		RegisterEntityBehaviorRemoveCommand(*cmd);
	}

	// (scene.entity.create parentId)
	// parentId == 0 -> no parent (root under scene)
	void SceneSystem::RegisterEntityCreateCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int>(
			"scene.entity.create",
			std::function<void(unsigned int)>(
			[this](unsigned int parentId)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
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
	void SceneSystem::RegisterEntityDestroyCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int, bool>(
			"scene.entity.destroy",
			std::function<void(unsigned int, bool)>(
			[this](unsigned int entityId, bool destroyChildren)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
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
	void SceneSystem::RegisterEntityAddComponentCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int, std::string>(
			"scene.entity.addComponent",
			std::function<void(unsigned int, std::string)>(
			[this](unsigned int entityId, std::string componentName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			AddComponentByName(*scene, entityId, componentName);
		}));
	}

	// (scene.entity.removeComponent entityId "ComponentName")
	void SceneSystem::RegisterEntityRemoveComponentCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int, std::string>(
			"scene.entity.removeComponent",
			std::function<void(unsigned int, std::string)>(
			[this](unsigned int entityId, std::string componentName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			RemoveComponentByName(*scene, entityId, componentName);
		}));
	}

	// (scene.entity.setMaterial entityId "MaterialKey")
	void SceneSystem::RegisterEntitySetMaterialCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int, std::string>(
			"scene.entity.setMaterial",
			std::function<void(unsigned int, std::string)>(
			[this](unsigned int entityId, std::string materialKey)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
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

			MaterialPool& materialPool = *services.Materials;

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

	void SceneSystem::RegisterEntityBehaviorAddCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int, std::string>(
			"scene.entity.addBehavior",
			std::function<void(unsigned int, std::string)>(
			[this](unsigned int entityId, std::string behaviorName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
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

	void SceneSystem::RegisterEntityBehaviorRemoveCommand(CommandSystem& cmd)
	{
		cmd.Register<unsigned int, std::string>(
			"scene.entity.removeBehavior",
			std::function<void(unsigned int, std::string)>(
			[this](unsigned int entityId, std::string behaviorName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
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
