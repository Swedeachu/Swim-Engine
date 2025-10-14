#pragma once

#include "Library/EnTT/entt.hpp"

#include "SubSceneSystems/SceneBVH.h"
#include "SubSceneSystems/GizmoSystem.h"
#include "SubSceneSystems/SceneDebugDraw.h"

#include "Engine/Components/ObjectTag.h"

#include "Engine/Systems/Entity/BehaviorComponents.h"
#include "Engine/Systems/Renderer/Core/MathTypes/MathAlgorithms.h"

#include <memory>

namespace Engine
{

	// Forward declaration of systems
	class SceneSystem;
	class InputManager;
	class CameraSystem;
	class VulkanRenderer;
	class OpenGLRenderer;
	class Renderer;

	// A scene contains a list (registry) of entities to store and update all their components each frame
	class Scene : public Machine, public std::enable_shared_from_this<Scene>
	{

	public:

		Scene() : name("UnnamedScene"), registry() {} // Default constructor

		// takes name param
		explicit Scene(const std::string& name = "scene")
			: name(name), registry()
		{}

		int Awake() override { return 0; };

		int Init() override { return 0; };

		void Update(double dt) override {};

		// Called before Scene::Awake
		void InternalSceneAwake();

		// Called before Scene::Init
		void InternalSceneInit();

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

		void DestroyEntity(entt::entity entity, bool callExit = true, bool destroyChildren = true);

		void DestroyAllEntities(bool callExit = true);

		void SetParent(entt::entity child, entt::entity parent);

		void RemoveParent(entt::entity child);

		std::vector<entt::entity>* GetChildren(entt::entity e);

		entt::entity GetParent(entt::entity e) const;

		const std::string& GetName() const { return name; }

		entt::registry& GetRegistry() { return registry; }

		// Called by SceneSystem during Awake
		void SetSceneSystem(const std::shared_ptr<SceneSystem>& system) { sceneSystem = system; }
		void SetInputManager(const std::shared_ptr<InputManager>& system) { inputManager = system; }
		void SetCameraSystem(const std::shared_ptr<CameraSystem>& system) { cameraSystem = system; }

		// Defined in C++ since it does some extra stuff for the ambiguous renderer
		void SetVulkanRenderer(const std::shared_ptr<VulkanRenderer>& system);
		void SetOpenGLRenderer(const std::shared_ptr<OpenGLRenderer>& system);

		// so much boiler plate for memory safety

		std::shared_ptr<SceneSystem> GetSceneSystem() const { return GetSystem<SceneSystem>(sceneSystem); }
		std::shared_ptr<InputManager> GetInputManager() const { return GetSystem<InputManager>(inputManager); }
		std::shared_ptr<CameraSystem> GetCameraSystem() const { return GetSystem<CameraSystem>(cameraSystem); }
		std::shared_ptr<VulkanRenderer> GetVulkanRenderer() const { return GetSystem<VulkanRenderer>(vulkanRenderer); }
		std::shared_ptr<OpenGLRenderer> GetOpenGLRenderer() const { return GetSystem<OpenGLRenderer>(openGLRenderer); }
		std::shared_ptr<Renderer> GetRenderer() const; // ambiguous version

		SceneBVH* GetSceneBVH() const { return sceneBVH.get(); }
		GizmoSystem* GetGizmoSystem() const { return gizmoSystem.get(); }
		SceneDebugDraw* GetSceneDebugDraw() const { return sceneDebugDraw.get(); }

		Ray ScreenPointToRay(const glm::vec2& point) const;

		template<typename T>
		T& AddComponent(entt::entity entity, T component)
		{
			static_assert(!std::is_reference_v<T>, "AddComponent should not take a reference type");
			static_assert(!std::is_pointer_v<T>, "AddComponent should not take a pointer type");

			return registry.emplace<T>(entity, std::move(component));
		}

		template<typename T, typename... Args>
		T& EmplaceComponent(entt::entity entity, Args&&... args)
		{
			static_assert(!std::is_pointer_v<T>, "EmplaceComponent should not take a pointer type");
			static_assert(std::is_constructible_v<T, Args&&...>, "T must be constructible with the provided arguments");

			return registry.emplace<T>(entity, std::forward<Args>(args)...);
		}

		// Adds an already-constructed behavior instance to an entity.
		// The behavior's Awake() and Init() methods are called immediately.
		// Returns a pointer to the behavior (typed as T*).
		template<typename T>
		T* AddBehavior(entt::entity entity, T behavior)
		{
			static_assert(std::is_base_of_v<Behavior, T>, "AddBehavior<T> requires T to derive from Behavior");
			static_assert(!std::is_pointer_v<T>, "AddBehavior should not take a pointer type");
			static_assert(!std::is_reference_v<T>, "AddBehavior should not take a reference type");

			// Wrap the behavior instance into a unique_ptr for BehaviorComponents
			auto uptr = std::make_unique<T>(std::move(behavior));
			T* raw = uptr.get();

			uptr->Awake();
			uptr->Init();

			auto& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.Add(std::move(uptr));

			return raw;
		}

		// Args is to contain any arguments for the dervied classes constructor that come after the explicit base class behavior constructor arguments.
		// The behavior's Awake() and Init() methods are called immediately.
		// Returns a pointer to the behavior created.
		template<typename T, typename... Args>
		T* EmplaceBehavior(entt::entity entity, Args&&... args)
		{
			static_assert(std::is_base_of_v<Behavior, T>, "EmplaceBehavior<T> requires T to derive from Behavior");

			auto uptr = std::make_unique<T>(this, entity, std::forward<Args>(args)...);
			T* raw = uptr.get();

			uptr->Awake();
			uptr->Init();

			auto& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.Add(std::move(uptr));

			return raw;
		}

		template<typename T>
		void RemoveBehavior(entt::entity entity, bool callExit = true)
		{
			static_assert(std::is_base_of_v<Behavior, T>, "RemoveBehavior<T> requires T to derive from Behavior");

			if (!registry.any_of<BehaviorComponents>(entity))
			{
				return;
			}

			auto& bc = registry.get<BehaviorComponents>(entity);
			auto& vec = bc.behaviors;

			// Remove behavior of type T
			vec.erase(std::remove_if(vec.begin(), vec.end(),
				[&](std::unique_ptr<Behavior>& b)
			{
				if (b && typeid(*b) == typeid(T))
				{
					if (callExit)
					{
						b->Exit();
					}
					return true;
				}
				return false;
			}), vec.end());

			// Remove component entirely if empty
			if (vec.empty())
			{
				registry.remove<BehaviorComponents>(entity);
			}
		}

		template<typename Func, typename... Args>
		void ForEachBehavior(Func method, Args&&... args)
		{
			registry.view<BehaviorComponents>().each(
				[&](auto entity, BehaviorComponents& bc)
			{
				for (auto& behavior : bc.behaviors)
				{
					if (behavior)
					{
						(behavior.get()->*method)(std::forward<Args>(args)...);
					}
				}
			});
		}

		template<typename Func, typename... Args>
		void ForEachBehaviorOfEntity(entt::entity entity, Func method, Args&&... args)
		{
			if (registry.valid(entity) && registry.any_of<BehaviorComponents>(entity))
			{
				auto& bc = registry.get<BehaviorComponents>(entity);
				for (auto& behavior : bc.behaviors)
				{
					if (behavior)
					{
						(behavior.get()->*method)(std::forward<Args>(args)...);
					}
				}
			}
		}

		ObjectTag* GetTag(entt::entity entity);

		void SetTag(entt::entity entity, int tag, const std::string& name = "");

		void RemoveTag(entt::entity entity);

		bool IsMouseBusyWithUI() const { return mouseBusyWithUI; }

	protected:

		std::string name;

		entt::registry registry;

		template <typename T>
		std::shared_ptr<T> GetSystem(const std::weak_ptr<T>& weakPtr) const
		{
			auto system = weakPtr.lock();
			if (!system)
			{
				// this seems like its relying heavily on RTTI
				// throw std::runtime_error(std::string(typeid(T).name()) + " is no longer valid!");
				throw std::runtime_error("Invalid System!");
			}
			return system;
		}

	private:

		std::weak_ptr<SceneSystem> sceneSystem;
		std::weak_ptr<InputManager> inputManager;
		std::weak_ptr<CameraSystem> cameraSystem;
		std::weak_ptr<VulkanRenderer> vulkanRenderer;
		std::weak_ptr<OpenGLRenderer> openGLRenderer;
		std::weak_ptr<Renderer> renderer;

		// Internals:
		entt::observer frustumCacheObserver;

		std::unique_ptr<SceneBVH> sceneBVH;
		std::unique_ptr<SceneDebugDraw> sceneDebugDraw;
		std::unique_ptr<GizmoSystem> gizmoSystem;

		void RemoveFrustumCache(entt::registry& registry, entt::entity entity);

		void UpdateUIBehaviors();

		bool WouldCreateCycle(const entt::registry& reg, entt::entity child, entt::entity newParent);

		bool mouseBusyWithUI{ false }; // to avoid interacting with world same time as interacting with UI above the world

	};

}
