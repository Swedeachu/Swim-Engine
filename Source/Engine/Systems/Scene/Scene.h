#pragma once

#include <entt/entt.hpp>

#include "Identity/EntityIdentityMap.h"
#include "TransformSystem.h"

#include "Engine/Components/Tags.h"
#include "Engine/EngineState.h"
#include "Engine/Machine.h"
#include "Engine/Runtime/SimulationClock.h"
#include "Engine/Systems/Entity/BehaviorComponents.h"

#include "Engine/Systems/Physics/PhysicsWorld.h"
#include "Physics/ScenePhysicsBridge.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Swim::Platform
{
	class FileSystem;
}

namespace Swim::Jobs
{
	class JobSystem;
}

namespace Swim::IO
{
	class AsyncIoService;
}

namespace Swim::Assets
{
	class AssetSystem;
}

namespace Swim::Memory
{
	class FrameArena;
}

namespace Swim::Input
{
	class InputSystem;
}

namespace Swim::Commands
{
	class CommandRegistry;
}

namespace Engine
{
	class PhysicsSystem;
	class CameraSystem;
	class SceneCommandBuffer;
	class BehaviorRegistry;
	class EngineStateMachine;
	struct RenderServices;
	class UiRuntime;

	// Non-owning engine services a scene uses. SceneSystem injects them before Awake;
	// only the core services are required (tests run scenes headless without input,
	// camera or rendering).
	struct SceneServices
	{
		Swim::Platform::FileSystem* Files = nullptr;
		Swim::Jobs::JobSystem* Jobs = nullptr;
		Swim::IO::AsyncIoService* IO = nullptr;
		Swim::Assets::AssetSystem* Assets = nullptr;
		Swim::Memory::FrameArena* FrameMemory = nullptr;
		const EngineStateMachine* State = nullptr;
		const SimulationFrame* Time = nullptr;
		const SimulationClock* Clock = nullptr; // Time scale, fixed rate, totals (optional).
		BehaviorRegistry* Behaviors = nullptr;
		TagRegistry* Tags = nullptr;
		PhysicsSystem* Physics = nullptr;
		// Presentation (optional).
		Swim::Input::InputSystem* Input = nullptr;
		CameraSystem* Camera = nullptr;
		RenderServices* Render = nullptr;
		// Tools (optional). Scenes may register their own console commands (from Awake).
		Swim::Commands::CommandRegistry* Commands = nullptr;
		std::function<bool(std::string_view)> DispatchCommand;
		std::function<int()> GetFPS;

		bool HasCore() const { return Files && Jobs && IO && Assets && FrameMemory && State && Time; }
	};

	// Result of a physics ray cast resolved to the entity that owns the body.
	struct SceneRaycastHit
	{
		entt::entity Entity = entt::null;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		float Distance = 0.0f;
	};

	// A scene owns an EnTT registry of entities, their hierarchy (Transform), names and
	// tags, behaviours, and a physics world. Game scenes derive from it and override
	// Awake/Init/Update/FixedUpdate/Exit and OnStateChanged. Rendering reads the
	// registry through the engine's render bridge (MeshRenderer, Light, ParticleEmitter,
	// UiCanvas components); the scene itself knows nothing about the renderer.
	class Scene : public Machine, public std::enable_shared_from_this<Scene>
	{
	  public:
		Scene();
		explicit Scene(const std::string& name);
		~Scene() override;

		int Awake() override { return 0; }

		int Init() override { return 0; }

		void Update(double dt) override {}

		void FixedUpdate(unsigned int tickThisSecond) override {}

		// Scene-specific teardown. Entities (and their behaviours' Exit) are destroyed
		// afterwards by InternalSceneExit.
		int Exit() override { return 0; }

		// Engine state transitions (after every behaviour received its hook).
		virtual void OnStateChanged(EngineState previous, EngineState current) {}

		// --- Called by SceneSystem ---
		void InternalSceneAwake();
		void InternalSceneInit();
		void InternalScenePostInit();
		void InternalSceneUpdate(double dt);
		void InternalScenePostUpdate(double dt);
		void InternalFixedUpdate(unsigned int tickThisSecond);
		void InternalFixedPostUpdate(unsigned int tickThisSecond);
		void InternalSceneExit();
		void InternalStateChanged(EngineState previous, EngineState current);

		// --- Entities ---
		entt::entity CreateEntity();
		entt::entity CreateEntity(std::string_view name);
		entt::entity CreateEntityWithSerializedId(SerializedEntityId id);
		SerializedEntityId GetSerializedEntityId(entt::entity entity) const;
		entt::entity FindEntityBySerializedId(SerializedEntityId id) const;

		bool IsValid(entt::entity entity) const { return registry.valid(entity); }

		void DestroyEntity(entt::entity entity, bool callExit = true, bool destroyChildren = true);
		void DestroyAllEntities(bool callExit = true);
		std::size_t GetEntityCount() const;

		void SetParent(entt::entity child, entt::entity parent);
		void RemoveParent(entt::entity child);
		std::vector<entt::entity>* GetChildren(entt::entity e);
		entt::entity GetParent(entt::entity e) const;

		const std::string& GetName() const { return name; }

		entt::registry& GetRegistry() { return registry; }

		const entt::registry& GetRegistry() const { return registry; }

		// --- Names and tags ---
		void SetEntityName(entt::entity entity, std::string_view value);
		// The EntityName, else "Entity <durable id>".
		std::string GetEntityName(entt::entity entity) const;
		entt::entity FindByName(std::string_view value) const;

		TagId AddTag(entt::entity entity, std::string_view tag); // Registers the name.
		bool AddTag(entt::entity entity, TagId tag);
		bool RemoveTag(entt::entity entity, TagId tag);
		bool HasTag(entt::entity entity, TagId tag) const;
		const TagSet* GetTags(entt::entity entity) const;
		// Entities with a tag, in no particular order (a snapshot: safe to mutate while iterating).
		std::vector<entt::entity> GetEntitiesWithTag(TagId tag) const;
		std::size_t CountWithTag(TagId tag) const;
		entt::entity FindFirstWithTag(TagId tag) const;

		template <typename Func> void ForEachWithTag(TagId tag, Func&& func)
		{
			for (const entt::entity entity : GetEntitiesWithTag(tag))
			{
				if (registry.valid(entity))
				{
					func(entity);
				}
			}
		}

		TagRegistry& GetTagRegistry() const;

		// --- Services ---
		void SetServices(SceneServices value);

		const SceneServices& GetServices() const { return services; }

		Swim::Input::InputSystem* GetInputSystem() const { return services.Input; }

		CameraSystem* GetCameraSystem() const { return services.Camera; }

		RenderServices* GetRenderServices() const { return services.Render; }

		EngineState GetEngineState() const;
		// The state behaviours run under this frame: the engine state, except that a single
		// step taken while paused (SimulationFrame::Stepped) runs as Playing.
		EngineState GetExecutionState() const;

		const EngineStateMachine* GetStateMachine() const { return services.State; }

		const SimulationFrame& GetTime() const;

		const SimulationClock* GetClock() const { return services.Clock; }

		Swim::Platform::FileSystem& GetFileSystem() const { return *Require(services.Files); }

		Swim::Jobs::JobSystem& GetJobSystem() const { return *Require(services.Jobs); }

		Swim::IO::AsyncIoService& GetIoSystem() const { return *Require(services.IO); }

		Swim::Assets::AssetSystem& GetAssetSystem() const { return *Require(services.Assets); }

		BehaviorRegistry& GetBehaviorRegistry() const { return *Require(services.Behaviors); }

		Swim::Memory::FrameArena& GetFrameArena() const { return *Require(services.FrameMemory); }

		SceneCommandBuffer& GetCommandBuffer() const { return *Require(sceneCommandBuffer.get()); }

		int GetFPS() const { return services.GetFPS ? services.GetFPS() : 0; }

		bool DispatchCommand(std::string_view command) const { return services.DispatchCommand && services.DispatchCommand(command); }

		TransformSystem& GetTransformSystem() { return transformSystem; }

		const TransformSystem& GetTransformSystem() const { return transformSystem; }

		void BeginFrameTransformTracking() { transformSystem.BeginFrame(); }

		// --- Components ---
		template <typename T> decltype(auto) AddComponent(entt::entity entity, T component)
		{
			static_assert(!std::is_reference_v<T>, "AddComponent should not take a reference type");
			static_assert(!std::is_pointer_v<T>, "AddComponent should not take a pointer type");

			using EmplaceResult = decltype(registry.emplace<T>(entity, std::move(component)));
			if constexpr (std::is_void_v<EmplaceResult>)
			{
				registry.emplace<T>(entity, std::move(component));
				return;
			}
			else
			{
				return registry.emplace<T>(entity, std::move(component));
			}
		}

		template <typename T, typename... Args> decltype(auto) EmplaceComponent(entt::entity entity, Args&&... args)
		{
			static_assert(!std::is_pointer_v<T>, "EmplaceComponent should not take a pointer type");
			static_assert(std::is_constructible_v<T, Args&&...>, "T must be constructible with the provided arguments");

			using EmplaceResult = decltype(registry.emplace<T>(entity, std::forward<Args>(args)...));
			if constexpr (std::is_void_v<EmplaceResult>)
			{
				registry.emplace<T>(entity, std::forward<Args>(args)...);
				return;
			}
			else
			{
				return registry.emplace<T>(entity, std::forward<Args>(args)...);
			}
		}

		template <typename T> bool RemoveComponent(entt::entity entity)
		{
			static_assert(!std::is_pointer_v<T>, "RemoveComponent should not take a pointer type");
			static_assert(!std::is_reference_v<T>, "RemoveComponent should not take a reference type");

			if (!registry.valid(entity) || !registry.any_of<T>(entity))
			{
				return false;
			}

			// Behaviours get Exit() before their storage goes away.
			if constexpr (std::is_same_v<T, BehaviorComponents>)
			{
				auto& bc = registry.get<BehaviorComponents>(entity);
				for (auto& b : bc.behaviors)
				{
					if (b && b->HasInited())
					{
						b->Exit();
					}
				}
			}

			registry.remove<T>(entity);
			return true;
		}

		// --- Behaviours ---
		// Adds an already-constructed behaviour; Awake runs after attachment and Init
		// before its first Update/FixedUpdate.
		template <typename T> T* AddBehavior(entt::entity entity, T&& behavior)
		{
			static_assert(std::is_base_of_v<Behavior, std::remove_reference_t<T>>, "AddBehavior<T> requires T to derive from Behavior");
			auto uptr = std::make_unique<std::remove_reference_t<T>>(std::forward<T>(behavior));
			return Attach(entity, std::move(uptr));
		}

		// Constructs T(scene, entity, args...) in place and adds it.
		template <typename T, typename... Args> T* EmplaceBehavior(entt::entity entity, Args&&... args)
		{
			static_assert(std::is_base_of_v<Behavior, T>, "EmplaceBehavior<T> requires T to derive from Behavior");
			auto uptr = std::make_unique<T>(this, entity, std::forward<Args>(args)...);
			return Attach(entity, std::move(uptr));
		}

		template <typename T> T* GetBehavior(entt::entity entity) const
		{
			if (!registry.valid(entity))
			{
				return nullptr;
			}
			const auto* bc = registry.try_get<BehaviorComponents>(entity);
			if (!bc)
			{
				return nullptr;
			}
			for (const auto& behavior : bc->behaviors)
			{
				if (auto* typed = dynamic_cast<T*>(behavior.get()))
				{
					return typed;
				}
			}
			return nullptr;
		}

		template <typename T> void RemoveBehavior(entt::entity entity, bool callExit = true)
		{
			static_assert(std::is_base_of_v<Behavior, T>, "RemoveBehavior<T> requires T to derive from Behavior");
			if (!registry.valid(entity) || !registry.any_of<BehaviorComponents>(entity))
			{
				return;
			}
			auto& vec = registry.get<BehaviorComponents>(entity).behaviors;
			vec.erase(std::remove_if(vec.begin(), vec.end(),
						  [&](std::unique_ptr<Behavior>& b)
						  {
							  if (b && typeid(*b) == typeid(T))
							  {
								  if (callExit && b->HasInited())
								  {
									  b->Exit();
								  }
								  return true;
							  }
							  return false;
						  }),
				vec.end());
		}

		Behavior* EmplaceBehaviorByName(entt::entity e, const std::string& behaviorName);
		bool RemoveBehaviorByName(entt::entity e, const std::string& behaviorName, bool callExit = true);
		void RefreshBehaviorFieldCacheForEntity(entt::entity e);

		void SetEnabledStates(entt::entity entity, EngineState states);
		void AddEnabledStates(entt::entity entity, EngineState states);
		void RemoveEnabledStates(entt::entity entity, EngineState states);

		// Calls method on every behaviour that can run in the current state.
		template <typename Func, typename... Args> void ForEachBehavior(Func method, Args&&... args)
		{
			const EngineState state = GetExecutionState();
			for (const entt::entity entity : SnapshotBehaviorEntities())
			{
				auto* bc = registry.valid(entity) ? registry.try_get<BehaviorComponents>(entity) : nullptr;
				if (!bc || !bc->CanExecute(state))
				{
					continue;
				}
				for (std::size_t i = 0; i < bc->behaviors.size(); ++i)
				{
					if (Behavior* behavior = bc->behaviors[i].get())
					{
						(behavior->*method)(args...);
					}
				}
			}
		}

		// As ForEachBehavior, initializing behaviours on their first call.
		template <typename Func, typename... Args> void ForEachInitializedBehavior(Func method, Args&&... args)
		{
			const EngineState state = GetExecutionState();
			for (const entt::entity entity : SnapshotBehaviorEntities())
			{
				auto* bc = registry.valid(entity) ? registry.try_get<BehaviorComponents>(entity) : nullptr;
				if (!bc || !bc->CanExecute(state))
				{
					continue;
				}
				// Index loop: a behaviour may add another to its own entity.
				for (std::size_t i = 0; i < bc->behaviors.size(); ++i)
				{
					Behavior* behavior = bc->behaviors[i].get();
					if (!behavior)
					{
						continue;
					}
					behavior->InitIfNeeded();
					(behavior->*method)(args...);
					// The callback may have destroyed components; re-fetch.
					bc = registry.valid(entity) ? registry.try_get<BehaviorComponents>(entity) : nullptr;
					if (!bc)
					{
						break;
					}
				}
			}
		}

		// --- Physics ---
		PhysicsWorld* GetPhysicsWorld() const;
		PhysicsWorld& GetOrCreatePhysicsWorld(PhysicsSystem& physicsSystem);

		ScenePhysicsBridge* GetPhysicsBridge() const { return physicsBridge.get(); }

		// Interpolates dynamic bodies between fixed steps (alpha from the SimulationFrame).
		void UpdatePhysics(PhysicsSystem& physicsSystem, float alpha);
		// One fixed physics step of dt seconds, then collision callbacks to behaviours.
		void FixedUpdatePhysics(PhysicsSystem& physicsSystem, float dt);
		void DestroyPhysicsWorld();
		std::optional<SceneRaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;
		entt::entity FindEntityByBody(BodyHandle body) const;

		std::uint64_t GetPhysicsStepCount() const { return physicsSteps; }

	  protected:
		std::string name;
		entt::registry registry;

		template <typename T> T* Require(T* system) const
		{
			if (!system)
			{
				throw std::runtime_error("Scene '" + name + "' is missing a required engine service.");
			}
			return system;
		}

		template <typename T> T* Attach(entt::entity entity, std::unique_ptr<T> uptr)
		{
			T* raw = uptr.get();
			auto& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.Add(std::move(uptr));
			raw->Awake();
			return raw;
		}

	  private:
		std::vector<entt::entity> SnapshotBehaviorEntities() const;
		void DispatchCollisionEvents();
		void OnTagSetDestroyed(entt::registry& reg, entt::entity entity);
		bool WouldCreateCycle(const entt::registry& reg, entt::entity child, entt::entity newParent);

		template <typename T> void OnComponentConstruct(entt::registry& reg, entt::entity entity);

		TransformSystem transformSystem;
		EntityIdentityMap entityIdentities;
		SceneServices services;
		std::unordered_map<std::uint32_t, std::unordered_set<entt::entity>> tagIndex;
		std::unique_ptr<TagRegistry> ownTags; // When no registry was injected (tests).
		bool transformHooksBound = false;
		bool tagHooksBound = false;

		std::unique_ptr<SceneCommandBuffer> sceneCommandBuffer;
		std::unique_ptr<ScenePhysicsBridge> physicsBridge;
		std::uint64_t physicsSteps = 0;
	};
} // namespace Engine
