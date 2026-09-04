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
		scene.SetInputManager(services.Presentation.Input);
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

			return commands->ParseAndDispatch(std::string(command));
		});

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


		return ok;
	}

	#if 0
	// DORMANT LEGACY EXTERNAL-EDITOR PROTOCOL
	// This code is intentionally excluded from the runtime build but retained in-tree as
	// historical/reference material. New editor features belong to Swim Engine's internal UI.
	// Small helpers used by the add/remove component commands:

	void SceneSystem::AddComponentByName(Scene& scene, SerializedEntityId entityId, const std::string& componentName)
	{
		entt::registry& reg = scene.GetRegistry();
		const entt::entity entity = scene.FindEntityBySerializedId(entityId);

		if (entity == entt::null || !reg.valid(entity))
		{
			return;
		}

		if (componentName == "Transform")
		{
			if (!reg.any_of<Transform>(entity))
			{
				scene.EmplaceComponent<Transform>(entity);
			}
		}
		else if (componentName == "Material")
		{
			if (!reg.any_of<Material>(entity))
			{
				scene.EmplaceComponent<Material>(entity);
			}
		}
		else if (componentName == "ObjectTag")
		{
			if (!reg.any_of<ObjectTag>(entity))
			{
				const std::string name = scene.GetEntityName(entity);
				scene.EmplaceComponent<ObjectTag>(entity, TagConstants::WORLD, name);
			}
		}
		else
		{
			std::cout << "SceneSystem::AddComponentByName | Unknown component: " << componentName << std::endl;
		}
	}

	void SceneSystem::RemoveComponentByName(Scene& scene, SerializedEntityId entityId, const std::string& componentName)
	{
		entt::registry& reg = scene.GetRegistry();
		const entt::entity entity = scene.FindEntityBySerializedId(entityId);

		if (entity == entt::null || !reg.valid(entity))
		{
			return;
		}

		if (componentName == "Transform")
		{
			scene.RemoveComponent<Transform>(entity);
		}
		else if (componentName == "Material")
		{
			scene.RemoveComponent<Material>(entity);
			scene.RemoveComponent<CompositeMaterial>(entity);
		}
		else if (componentName == "ObjectTag")
		{
			scene.RemoveTag(entity);
		}
	}

	void SceneSystem::SendBehaviorsToEditor()
	{
		if (!services.Tools.SendEditorMessage)
		{
			return;
		}

		for (const BehaviorRegistry::Descriptor& descriptor : behaviorRegistry.GetDescriptors())
		{
			services.Tools.SendEditorMessage("loadBehavior " + descriptor.Name, /*channel:*/1);
		}
	}

	void SceneSystem::RegisterEditorCommands()
	{
		CommandSystem* cmd = services.Tools.Commands;
		if (!cmd)
		{
			return;
		}

		RegisterEntityCreateCommand(*cmd);
		RegisterEntityDestroyCommand(*cmd);
		RegisterEntityAddComponentCommand(*cmd);
		RegisterEntityRemoveComponentCommand(*cmd);
		RegisterEntitySetMaterialCommand(*cmd);
		RegisterEntityBehaviorAddCommand(*cmd);
		RegisterEntityBehaviorRemoveCommand(*cmd);
	}

	// Editor scene commands use durable SerializedEntityId values. Runtime EnTT
	// handles never cross the tooling boundary, and all mutations enter the owning
	// scene's command buffer before touching the registry.

	// (scene.entity.create parentId)
	// parentId == 0 -> no parent (root under scene)
	void SceneSystem::RegisterEntityCreateCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t>(
			"scene.entity.create",
			std::function<void(std::uint64_t)>(
			[this](std::uint64_t parentValue)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId parentId{ parentValue };
			scene->GetCommandBuffer().Create(
				[parentId](Scene& owningScene, entt::entity entity)
			{
				owningScene.EmplaceComponent<Transform>(entity);

				if (parentId)
				{
					const entt::entity parent = owningScene.FindEntityBySerializedId(parentId);
					if (parent != entt::null)
					{
						owningScene.SetParent(entity, parent);
					}
				}

				const std::string name = owningScene.GetEntityName(entity);
				owningScene.SetTag(entity, TagConstants::WORLD, name);
			});
		}));
	}

	// (scene.entity.destroy entityId destroyChildren)
	void SceneSystem::RegisterEntityDestroyCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t, bool>(
			"scene.entity.destroy",
			std::function<void(std::uint64_t, bool)>(
			[this](std::uint64_t entityValue, bool destroyChildren)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId entityId{ entityValue };
			scene->GetCommandBuffer().Defer(
				[entityId, destroyChildren](Scene& owningScene)
			{
				const entt::entity entity = owningScene.FindEntityBySerializedId(entityId);
				if (entity != entt::null)
				{
					owningScene.DestroyEntity(entity, true, destroyChildren);
				}
			});
		}));
	}

	// (scene.entity.addComponent entityId "ComponentName")
	void SceneSystem::RegisterEntityAddComponentCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t, std::string>(
			"scene.entity.addComponent",
			std::function<void(std::uint64_t, std::string)>(
			[this](std::uint64_t entityValue, std::string componentName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId entityId{ entityValue };
			scene->GetCommandBuffer().Defer(
				[entityId, componentName = std::move(componentName)](Scene& owningScene)
			{
				AddComponentByName(owningScene, entityId, componentName);
			});
		}));
	}

	// (scene.entity.removeComponent entityId "ComponentName")
	void SceneSystem::RegisterEntityRemoveComponentCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t, std::string>(
			"scene.entity.removeComponent",
			std::function<void(std::uint64_t, std::string)>(
			[this](std::uint64_t entityValue, std::string componentName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId entityId{ entityValue };
			scene->GetCommandBuffer().Defer(
				[entityId, componentName = std::move(componentName)](Scene& owningScene)
			{
				RemoveComponentByName(owningScene, entityId, componentName);
			});
		}));
	}

	// (scene.entity.setMaterial entityId "MaterialKey")
	void SceneSystem::RegisterEntitySetMaterialCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t, std::string>(
			"scene.entity.setMaterial",
			std::function<void(std::uint64_t, std::string)>(
			[this](std::uint64_t entityValue, std::string materialKey)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId entityId{ entityValue };
			scene->GetCommandBuffer().Defer(
				[entityId, materialKey = std::move(materialKey)](Scene& owningScene)
			{
				const entt::entity entity = owningScene.FindEntityBySerializedId(entityId);
				if (entity == entt::null || !owningScene.HasPresentationServices())
				{
					return;
				}

				MaterialPool& materialPool = owningScene.GetMaterialPool();

				if (materialPool.CompositeMaterialExists(materialKey))
				{
					try
					{
						auto data = materialPool.GetCompositeMaterialData(materialKey);
						owningScene.RemoveComponent<Material>(entity);
						owningScene.RemoveComponent<CompositeMaterial>(entity);
						owningScene.EmplaceComponent<CompositeMaterial>(
							entity,
							data,
							materialKey,
							materialPool.GetCompositeMaterialAssetId(materialKey));
						if (SceneBVH* bvh = owningScene.GetSceneBVH())
						{
							bvh->ForceUpdateNextFrame();
						}
						return;
					}
					catch (const std::exception& exception)
					{
						std::cout << exception.what() << std::endl;
						return;
					}
				}

				if (materialPool.MaterialExists(materialKey))
				{
					try
					{
						auto data = materialPool.GetMaterialBinding(materialKey);
						owningScene.RemoveComponent<Material>(entity);
						owningScene.RemoveComponent<CompositeMaterial>(entity);
						owningScene.EmplaceComponent<Material>(entity, data);
						if (SceneBVH* bvh = owningScene.GetSceneBVH())
						{
							bvh->ForceUpdateNextFrame();
						}
						return;
					}
					catch (const std::exception& exception)
					{
						std::cout << exception.what() << std::endl;
						return;
					}
				}

				std::cout << "Failed to apply material " << materialKey << std::endl;
			});
		}));
	}

	void SceneSystem::RegisterEntityBehaviorAddCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t, std::string>(
			"scene.entity.addBehavior",
			std::function<void(std::uint64_t, std::string)>(
			[this](std::uint64_t entityValue, std::string behaviorName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId entityId{ entityValue };
			scene->GetCommandBuffer().Defer(
				[entityId, behaviorName = std::move(behaviorName)](Scene& owningScene)
			{
				const entt::entity entity = owningScene.FindEntityBySerializedId(entityId);
				if (entity == entt::null)
				{
					return;
				}

				Behavior* behavior = owningScene.EmplaceBehaviorByName(entity, behaviorName);
				if (!behavior)
				{
					std::cout << "Failed to add behavior '" << behaviorName << "' to entity " << entityId.Value << std::endl;
				}
			});
		}));
	}

	void SceneSystem::RegisterEntityBehaviorRemoveCommand(CommandSystem& cmd)
	{
		cmd.Register<std::uint64_t, std::string>(
			"scene.entity.removeBehavior",
			std::function<void(std::uint64_t, std::string)>(
			[this](std::uint64_t entityValue, std::string behaviorName)
		{
			std::shared_ptr<Scene> scene = GetActiveScene();
			if (!scene)
			{
				return;
			}

			const SerializedEntityId entityId{ entityValue };
			scene->GetCommandBuffer().Defer(
				[entityId, behaviorName = std::move(behaviorName)](Scene& owningScene)
			{
				const entt::entity entity = owningScene.FindEntityBySerializedId(entityId);
				if (entity == entt::null)
				{
					return;
				}

				if (!owningScene.RemoveBehaviorByName(entity, behaviorName))
				{
					std::cout << "Failed to remove behavior '" << behaviorName << "' from entity " << entityId.Value << std::endl;
				}
			});
		}));
	}


	#endif

}
