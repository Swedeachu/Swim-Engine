// Historical SceneSystem member fragment. Reference only; never compiled.

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
