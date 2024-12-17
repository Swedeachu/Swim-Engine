#pragma once

#include "Library/EnTT/entt.hpp"

namespace Engine
{

	// Forward declaration of systems
	class SceneSystem;
	class InputManager;
	class CameraSystem;
	class VulkanRenderer;

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

		void FixedUpdate(unsigned int tickThisSecond) override {};

		int Exit() override { return 0; };

		entt::entity CreateEntity();

		void DestroyEntity(entt::entity entity);

		const std::string& GetName() const { return name; }

		entt::registry& GetRegistry() { return registry; }

		// Called by SceneSystem during Awake
		void SetSceneSystem(const std::shared_ptr<SceneSystem>& system) { sceneSystem = system; }
		void SetInputManager(const std::shared_ptr<InputManager>& system) { inputManager = system; }
		void SetCameraSystem(const std::shared_ptr<CameraSystem>& system) { cameraSystem = system; }
		void SetRenderer(const std::shared_ptr<VulkanRenderer>& system) { renderer = system; }

		// Pass meshes and transforms to the renderer, called on the active scene by the SceneSystem
		void SubmitMeshesToRenderer();

	protected:

		std::string name;

		entt::registry registry;

		// so much boiler plate for memory safety

		std::shared_ptr<SceneSystem> GetSceneSystem() const { return GetSystem<SceneSystem>(sceneSystem); }
		std::shared_ptr<InputManager> GetInputManager() const { return GetSystem<InputManager>(inputManager); }
		std::shared_ptr<CameraSystem> GetCameraSystem() const { return GetSystem<CameraSystem>(cameraSystem); }
		std::shared_ptr<VulkanRenderer> GetRenderer() const { return GetSystem<VulkanRenderer>(renderer); }

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
		std::weak_ptr<VulkanRenderer> renderer;

	};

}