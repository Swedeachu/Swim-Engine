#pragma once

#include "Library/EnTT/entt.hpp"
#include "SubSceneSystems/SceneBVH.h"
#include "SubSceneSystems/SceneDebugDraw.h"

namespace Engine
{

	// Forward declaration of systems
	class SceneSystem;
	class InputManager;
	class CameraSystem;
	class VulkanRenderer;
	class OpenGLRenderer;

	// A scene contains a list (registry) of entities to store and update all their components each frame
	class Scene : public Machine
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

		// All scenes have a base init and update for doing internal engine things first 
		void InternalSceneInit();

		// Called before Scene::Update
		void InternalSceneUpdate(double dt);

		// Called after Scene::Update
		void InternalScenePostUpdate(double dt);

		void FixedUpdate(unsigned int tickThisSecond) override {};

		// Called before Scene::FixedUpdate
		void InternalFixedUpdate(unsigned int tickThisSecond);

		// Called after Scene::FixedUpdate
		void InternalFixedPostUpdate(unsigned int tickThisSecond);

		int Exit() override { DestroyAllEntities(); return 0; };

		entt::entity CreateEntity();

		void DestroyEntity(entt::entity entity);

		void DestroyAllEntities();

		const std::string& GetName() const { return name; }

		entt::registry& GetRegistry() { return registry; }

		// Called by SceneSystem during Awake
		void SetSceneSystem(const std::shared_ptr<SceneSystem>& system) { sceneSystem = system; }
		void SetInputManager(const std::shared_ptr<InputManager>& system) { inputManager = system; }
		void SetCameraSystem(const std::shared_ptr<CameraSystem>& system) { cameraSystem = system; }
		void SetVulkanRenderer(const std::shared_ptr<VulkanRenderer>& system) { vulkanRenderer = system; }
		void SetOpenGLRenderer(const std::shared_ptr<OpenGLRenderer>& system) { openGLRenderer = system; }

		// so much boiler plate for memory safety

		std::shared_ptr<SceneSystem> GetSceneSystem() const { return GetSystem<SceneSystem>(sceneSystem); }
		std::shared_ptr<InputManager> GetInputManager() const { return GetSystem<InputManager>(inputManager); }
		std::shared_ptr<CameraSystem> GetCameraSystem() const { return GetSystem<CameraSystem>(cameraSystem); }
		std::shared_ptr<VulkanRenderer> GetVulkanRenderer() const { return GetSystem<VulkanRenderer>(vulkanRenderer); }
		std::shared_ptr<OpenGLRenderer> GetOpenGLRenderer() const { return GetSystem<OpenGLRenderer>(openGLRenderer); }

		SceneBVH* GetSceneBVH() const { return sceneBVH.get(); }
		SceneDebugDraw* GetSceneDebugDraw() const { return sceneDebugDraw.get(); }

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

		// Internals:
		entt::observer frustumCacheObserver;

		std::unique_ptr<SceneBVH> sceneBVH;
		std::unique_ptr<SceneDebugDraw> sceneDebugDraw;

		void RemoveFrustumCache(entt::registry& registry, entt::entity entity);

	};

}