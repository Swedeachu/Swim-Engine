#pragma once

#include "Library/EnTT/entt.hpp"

namespace Engine
{

	// Forward declaration of SceneSystem
	class SceneSystem;
	class InputManager;

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

	protected:

		std::string name;

		entt::registry registry;

		// so much boiler plate for memory safety, have you tried not writing shit code instead when working with raw ptrs? Would avoid this woke nonsense entirely

		std::shared_ptr<SceneSystem> GetSceneSystem() const { return GetSystem<SceneSystem>(sceneSystem); }
		std::shared_ptr<InputManager> GetInputManager() const { return GetSystem<InputManager>(inputManager); }

		template <typename T>
		std::shared_ptr<T> GetSystem(const std::weak_ptr<T>& weakPtr) const
		{
			auto system = weakPtr.lock();
			if (!system)
			{
				// this seems like its relying heavily on RTTI
				throw std::runtime_error(std::string(typeid(T).name()) + " is no longer valid!");
			}
			return system;
		}

	private:

		std::weak_ptr<SceneSystem> sceneSystem; 
		std::weak_ptr<InputManager> inputManager; 

	};

}