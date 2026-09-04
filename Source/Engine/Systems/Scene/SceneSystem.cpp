#include "PCH.h"
#include "SceneSystem.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"

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

		// Register editor-only command surfaces from the actual runtime state.
		if (HasAnyEngineStates(*services.Core.State, EngineState::Editing) && services.Tools.Commands)
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
		scene.SetInputManager(services.Presentation.Input);
		scene.SetCameraSystem(services.Presentation.Camera);
		scene.SetEngineState(services.Core.State);
		scene.SetClipSpaceDepthRange(services.Presentation.ClipDepth);
		scene.SetMeshPool(services.Presentation.Meshes);
		scene.SetTexturePool(services.Presentation.Textures);
		scene.SetMaterialPool(services.Presentation.Materials);
		scene.SetFontPool(services.Presentation.Fonts);
		scene.SetFileSystem(services.Core.Files);
		scene.SetJobSystem(services.Core.Jobs);
		scene.SetIoSystem(services.Core.IO);
		scene.SetAssetSystem(services.Core.Assets);
		scene.SetFrameArena(services.Core.FrameMemory);
		scene.SetFPSProvider(services.Tools.GetFPS);
		scene.SetCommandDispatcher([commands = services.Tools.Commands](std::string_view command)
		{
			if (!commands)
			{
				return false;
			}

			return commands->ParseAndDispatch(std::string(command));
		});
		scene.SetEditorMessageSender(services.Tools.SendEditorMessage);

		scene.SetCubeMapController(services.Presentation.CubeMap);
	}

	bool SceneSystem::DispatchCommand(std::string_view command)
	{
		if (!services.Tools.Commands)
		{
			return false;
		}

		const std::string ownedCommand(command);
		const bool ok = services.Tools.Commands->ParseAndDispatch(ownedCommand);

		if (services.Tools.SendEditorMessage)
		{
			services.Tools.SendEditorMessage(std::string(ok ? "(Recv [200]): " : "(Recv [400]): ") + ownedCommand, 1);
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
		if (!services.Tools.SendEditorMessage)
		{
			return;
		}

		auto behaviorFactories = BehaviorFactory::GetInstance().GetFactories();
		for (const auto& factory : behaviorFactories)
		{
			services.Tools.SendEditorMessage("loadBehavior " + factory.first, /*channel:*/1);
		}
	}

	// Command registration entry point
	void SceneSystem::RegisterEditorCommands()
	{
		CommandSystem* cmd = services.Tools.Commands;
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

			if (!services.Presentation.Materials)
			{
				return;
			}

			MaterialPool& materialPool = *services.Presentation.Materials;

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
					auto data = materialPool.GetMaterialBinding(materialKey);
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
