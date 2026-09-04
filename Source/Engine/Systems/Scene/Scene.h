#pragma once

#include <entt/entt.hpp>

#include "SubSceneSystems/SceneBVH.h"
#include "SubSceneSystems/GizmoSystem.h"
#include "SubSceneSystems/SceneDebugDraw.h"
#include "TransformSystem.h"
#include "Serialization/EntityIdentityMap.h"

#include "Engine/Components/ObjectTag.h"
#include "Engine/EngineState.h"

#include "Engine/Systems/Entity/BehaviorComponents.h"
#include "Engine/Systems/Renderer/Core/MathTypes/MathAlgorithms.h"
#include "Engine/Systems/Renderer/Core/RenderConventions.h"

#include "Engine/Systems/Physics/PhysicsWorld.h"
#include "Physics/ScenePhysicsBridge.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
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

namespace Engine
{

	// Forward declaration of systems
	class PhysicsSystem;
	class InputManager;
	class CameraSystem;
	class CubeMapController;
	class MeshPool;
	class TexturePool;
	class MaterialPool;
	class FontPool;
	class SceneCommandBuffer;
	class BehaviorRegistry;
	class SceneSerializer;
	class SceneStorage;
	class SceneToolingBridge;
	class SceneSyncTracker;

	// A scene contains a list (registry) of entities to store and update all their components each frame
	class Scene : public Machine, public std::enable_shared_from_this<Scene>
	{

	public:

		Scene();

		// takes name param
		explicit Scene(const std::string& name);

		~Scene() override;

		int Awake() override { return 0; };

		int Init() override { return 0; };

		void Update(double dt) override {};

		// Called before Scene::Awake
		void InternalSceneAwake();

		// Called before Scene::Init
		void InternalSceneInit();

		// Called after Scene::Init
		void InternalScenePostInit();

		// Called before Scene::Update
		void InternalSceneUpdate(double dt);

		// Called after Scene::Update
		void InternalScenePostUpdate(double dt);

		// Called before Scene::Exit
		void InternalSceneExit();

		void FixedUpdate(unsigned int tickThisSecond) override {};

		// Called before Scene::FixedUpdate
		void InternalFixedUpdate(unsigned int tickThisSecond);

		// Called after Scene::FixedUpdate
		void InternalFixedPostUpdate(unsigned int tickThisSecond);

		int Exit() override { DestroyAllEntities(); return 0; };

		entt::entity CreateEntity();
		entt::entity CreateEntityWithSerializedId(SerializedEntityId id);
		SerializedEntityId GetSerializedEntityId(entt::entity entity) const;
		entt::entity FindEntityBySerializedId(SerializedEntityId id) const;

		void DestroyEntity(entt::entity entity, bool callExit = true, bool destroyChildren = true);

		void DestroyAllEntities(bool callExit = true);

		void SetParent(entt::entity child, entt::entity parent);

		void RemoveParent(entt::entity child);

		std::vector<entt::entity>* GetChildren(entt::entity e);

		entt::entity GetParent(entt::entity e) const;

		const std::string& GetName() const { return name; }

		entt::registry& GetRegistry() { return registry; }

		// Check if an entity should only be rendered during editing.
		bool ShouldRenderOnlyDuringEditingBasedOnState(entt::entity e) const;

		// Check if an entity should render in general, this uses ShouldRenderOnlyDuringEditing. The renderer will call this in the render passes.
		bool ShouldRenderBasedOnState(entt::entity e) const;
		uint64_t GetRenderablesRevision() const { return renderablesRevision; }

		// Called by the scene owner during Awake. These are non-owning engine service views.
		void SetInputManager(InputManager* system) { inputManager = system; }
		void SetCameraSystem(CameraSystem* system) { cameraSystem = system; }
		void SetEngineState(const EngineState* state) { engineState = state; }
		void SetMeshPool(MeshPool* value) { meshPool = value; }
		void SetTexturePool(TexturePool* value) { texturePool = value; }
		void SetMaterialPool(MaterialPool* value) { materialPool = value; }
		void SetFontPool(FontPool* value) { fontPool = value; }
		void SetFileSystem(Swim::Platform::FileSystem* value) { fileSystem = value; }
		void SetJobSystem(Swim::Jobs::JobSystem* value) { jobSystem = value; }
		void SetIoSystem(Swim::IO::AsyncIoService* value) { ioSystem = value; }
		void SetAssetSystem(Swim::Assets::AssetSystem* value) { assetSystem = value; }
		void SetBehaviorRegistry(BehaviorRegistry* value) { behaviorRegistry = value; }
		void SetFrameArena(Swim::Memory::FrameArena* value) { frameArena = value; }
		void SetFPSProvider(std::function<int()> provider) { fpsProvider = std::move(provider); }
		void SetCommandDispatcher(std::function<bool(std::string_view)> dispatcher) { commandDispatcher = std::move(dispatcher); }
		void SetEditorMessageSender(std::function<bool(const std::string&, std::uintptr_t)> sender) { editorMessageSender = std::move(sender); }

		void SetCubeMapController(CubeMapController* value) { cubeMapController = value; }

		InputManager* GetInputManager() const { return inputManager; }
		CameraSystem* GetCameraSystem() const { return cameraSystem; }
		CubeMapController* GetCubeMapController() const { return cubeMapController; }
		EngineState GetEngineState() const { return *GetSystem(engineState); }
		MeshPool& GetMeshPool() const { return *GetSystem(meshPool); }
		TexturePool& GetTexturePool() const { return *GetSystem(texturePool); }
		MaterialPool& GetMaterialPool() const { return *GetSystem(materialPool); }
		FontPool& GetFontPool() const { return *GetSystem(fontPool); }
		Swim::Platform::FileSystem& GetFileSystem() const { return *GetSystem(fileSystem); }
		Swim::Jobs::JobSystem& GetJobSystem() const { return *GetSystem(jobSystem); }
		Swim::IO::AsyncIoService& GetIoSystem() const { return *GetSystem(ioSystem); }
		Swim::Assets::AssetSystem& GetAssetSystem() const { return *GetSystem(assetSystem); }
		BehaviorRegistry& GetBehaviorRegistry() const { return *GetSystem(behaviorRegistry); }
		Swim::Memory::FrameArena& GetFrameArena() const { return *GetSystem(frameArena); }
		SceneCommandBuffer& GetCommandBuffer() const { return *GetSystem(sceneCommandBuffer.get()); }
		int GetFPS() const { return fpsProvider ? fpsProvider() : 0; }
		bool DispatchCommand(std::string_view command) const { return commandDispatcher && commandDispatcher(command); }
		bool SendEditorMessage(const std::string& message, std::uintptr_t channel = 1) const
		{
			return editorMessageSender && editorMessageSender(message, channel);
		}
		bool HasPresentationServices() const
		{
			return inputManager && cameraSystem && meshPool && texturePool && materialPool && fontPool;
		}

		TransformSystem& GetTransformSystem() { return transformSystem; }
		const TransformSystem& GetTransformSystem() const { return transformSystem; }
		void BeginFrameTransformTracking() { transformSystem.BeginFrame(); }

		SceneBVH* GetSceneBVH() const { return sceneBVH.get(); }
		GizmoSystem* GetGizmoSystem() const { return gizmoSystem.get(); }
		SceneDebugDraw* GetSceneDebugDraw() const { return sceneDebugDraw.get(); }

		Ray ScreenPointToRay(const glm::vec2& point) const;

		bool IsTopFocusedElement(entt::entity target);
		bool IsTopMostUiAtScreenPoint(entt::entity target, const glm::vec2& point);

		template<typename T>
		decltype(auto) AddComponent(entt::entity entity, T component)
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

		template<typename T, typename... Args>
		decltype(auto) EmplaceComponent(entt::entity entity, Args&&... args)
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

		template<typename T>
		bool RemoveComponent(entt::entity entity)
		{
			static_assert(!std::is_pointer_v<T>, "RemoveComponent should not take a pointer type");
			static_assert(!std::is_reference_v<T>, "RemoveComponent should not take a reference type");

			if (!registry.valid(entity) || !registry.any_of<T>(entity))
			{
				return false;
			}

			// Special handling for BehaviorComponents so we properly Exit() behaviors.
			if constexpr (std::is_same_v<T, BehaviorComponents>)
			{
				EngineState state = GetEngineState();
				auto& bc = registry.get<BehaviorComponents>(entity);
				if (bc.CanExecute(state))
				{
					for (auto& b : bc.behaviors)
					{
						if (b) { b->Exit(); }
					}
				}
			}

			registry.remove<T>(entity);

			// All serialization notifications are now driven by registry hooks.

			return true;
		}

		// Adds an already-constructed behavior instance to an entity.
		// The behavior's Awake() is called AFTER ownership transfer.
		// Init() is called immediately if CanExecute(current engine state) is true.
		// Returns T* to the stored behavior.
		template<typename T>
		T* AddBehavior(entt::entity entity, T&& behavior)
		{
			static_assert(std::is_base_of_v<Behavior, std::remove_reference_t<T>>,
				"AddBehavior<T> requires T to derive from Behavior");
			static_assert(!std::is_pointer_v<std::remove_reference_t<T>>,
				"AddBehavior should not take a pointer type");

			auto uptr = std::make_unique<std::remove_reference_t<T>>(std::forward<T>(behavior));
			return AttachAwakeInit(entity, std::move(uptr));
		}

		// Constructs the behavior in-place using (this, entity, args...) and adds it.
		// Awake() happens after attach; Init() is immediate if CanExecute(...) is true.
		// Returns T* to the stored behavior.
		template<typename T, typename... Args>
		T* EmplaceBehavior(entt::entity entity, Args&&... args)
		{
			static_assert(std::is_base_of_v<Behavior, T>,
				"EmplaceBehavior<T> requires T to derive from Behavior");

			auto uptr = std::make_unique<T>(this, entity, std::forward<Args>(args)...);
			return AttachAwakeInit(entity, std::move(uptr));
		}

		template<typename T>
		void RemoveBehavior(entt::entity entity, bool callExit = true)
		{
			static_assert(std::is_base_of_v<Behavior, T>, "RemoveBehavior<T> requires T to derive from Behavior");

			if (!registry.any_of<BehaviorComponents>(entity))
			{
				return;
			}

			EngineState state = GetEngineState();

			auto& bc = registry.get<BehaviorComponents>(entity);
			auto& vec = bc.behaviors;

			// Remove behavior of type T
			vec.erase(std::remove_if(vec.begin(), vec.end(),
				[&](std::unique_ptr<Behavior>& b)
			{
				if (b && typeid(*b) == typeid(T))
				{
					if (callExit && bc.CanExecute(state))
					{
						b->Exit();
					}
					return true;
				}
				return false;
			}), vec.end());
		}

		Behavior* EmplaceBehaviorByName(entt::entity e, const std::string& behaviorName);
		bool RemoveBehaviorByName(entt::entity e, const std::string& behaviorName, bool callExit = true);

		// Calls Behavior::RefreshFieldCache() on each behavior the entity has
		void RefreshBehaviorFieldCacheForEntity(entt::entity e);

		template<typename Func, typename... Args>
		void ForEachBehavior(Func method, Args&&... args)
		{
			EngineState state = GetEngineState();

			registry.view<BehaviorComponents>().each(
				[&](auto entity, BehaviorComponents& bc)
			{
				if (bc.CanExecute(state))
				{
					for (auto& behavior : bc.behaviors)
					{
						if (behavior)
						{
							(behavior.get()->*method)(std::forward<Args>(args)...);
						}
					}
				}
			});
		}

		void SetEnabledStates(entt::entity entity, EngineState states);

		void AddEnabledStates(entt::entity entity, EngineState states);

		void RemoveEnabledStates(entt::entity entity, EngineState states);

		bool StateTestControl();

		ObjectTag* GetTag(entt::entity entity);
		const std::string GetEntityName(entt::entity e) const;
		void SetTag(entt::entity entity, unsigned int tag, const std::string& name = "");
		void RemoveTag(entt::entity entity);

		bool IsMouseBusyWithUI() const { return mouseBusyWithUI; }

		PhysicsWorld* GetPhysicsWorld() const;

		PhysicsWorld& GetOrCreatePhysicsWorld(PhysicsSystem& physicsSystem);

		void UpdatePhysics(PhysicsSystem& physicsSystem, double dt);

		void FixedUpdatePhysics(PhysicsSystem& physicsSystem);

		void DestroyPhysicsWorld();

	protected:

		std::string name;

		entt::registry registry;

		template <typename T>
		T* GetSystem(T* system) const
		{
			if (!system)
			{
				throw std::runtime_error("Invalid System!");
			}
			return system;
		}

		template<typename T>
		T* AttachAwakeInit(entt::entity entity, std::unique_ptr<T> uptr)
		{
			T* raw = uptr.get();

			// Add to behavior components first (so it's owned)
			auto& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.Add(std::move(uptr));

			// Call awake
			raw->Awake();

			// Conditionally Init immediately if it can execute in the current state
			/* We actually defer until the next frame for maximum safety.
			const EngineState state = GetEngineState();
			if (bc.CanExecute(state))
			{
				raw->SetInited();
				raw->Init();
			}
			*/

			return raw;
		}

	private:

		uint64_t renderablesRevision{ 0 };
		TransformSystem transformSystem;
		EntityIdentityMap entityIdentities;

		InputManager* inputManager = nullptr;
		CameraSystem* cameraSystem = nullptr;
		CubeMapController* cubeMapController = nullptr;
		const EngineState* engineState = nullptr;
		MeshPool* meshPool = nullptr;
		TexturePool* texturePool = nullptr;
		MaterialPool* materialPool = nullptr;
		FontPool* fontPool = nullptr;
		Swim::Platform::FileSystem* fileSystem = nullptr;
		Swim::Jobs::JobSystem* jobSystem = nullptr;
		Swim::IO::AsyncIoService* ioSystem = nullptr;
		Swim::Assets::AssetSystem* assetSystem = nullptr;
		BehaviorRegistry* behaviorRegistry = nullptr;
		Swim::Memory::FrameArena* frameArena = nullptr;
		std::function<int()> fpsProvider;
		std::function<bool(std::string_view)> commandDispatcher;
		std::function<bool(const std::string&, std::uintptr_t)> editorMessageSender;
		bool transformHooksBound{ false };
		bool serializationHooksBound{ false };

		// Internals:
		entt::observer frustumCacheObserver;

		std::unique_ptr<SceneCommandBuffer> sceneCommandBuffer;
		std::unique_ptr<SceneBVH> sceneBVH;
		std::unique_ptr<ScenePhysicsBridge> physicsBridge;
		double physicsTimeSinceLastTick{ 0.0 };
		std::unique_ptr<SceneDebugDraw> sceneDebugDraw;
		std::unique_ptr<GizmoSystem> gizmoSystem;
		std::unique_ptr<SceneSerializer> sceneSerializer;
		std::unique_ptr<SceneStorage> sceneStorage;
		std::unique_ptr<SceneToolingBridge> sceneToolingBridge;
		std::unique_ptr<SceneSyncTracker> sceneSyncTracker;

		void RemoveFrustumCache(entt::registry& registry, entt::entity entity);

		void UpdateUIBehaviors();

		bool WouldCreateCycle(const entt::registry& reg, entt::entity child, entt::entity newParent);

		bool mouseBusyWithUI{ false }; // to avoid interacting with world same time as interacting with UI above the world

		// --- Serialization bindings driven by the registry ---

		template<typename T>
		void BindSerializationHooksForComponent();

		template<typename T>
		void OnComponentConstruct(entt::registry& reg, entt::entity entity);

		template<typename T>
		void OnComponentDestroy(entt::registry& reg, entt::entity entity);

	};

}
