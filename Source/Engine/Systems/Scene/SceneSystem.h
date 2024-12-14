#pragma once

#include "Scene.h"

namespace Engine
{

	class SceneSystem : public Machine
	{

	public:

		static void Preregister(std::shared_ptr<Scene> scene);

		int Awake() override;

		int Init() override;

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) override;

		int Exit() override;

		template <typename T, typename... Args>
		void RegisterScene(const std::string& name, Args&&... args)
		{
			scenes[name] = std::make_shared<T>(std::forward<Args>(args)...);
		}

		// Sets the active scene by name, optionally exiting the current one
		void SetScene(const std::string& name, bool exitCurrent = true);

	private:

		// Map of scenes by name
		std::map<std::string, std::shared_ptr<Scene>> scenes;

		// Vector of scenes we will auto register at 
		static std::vector<std::shared_ptr<Scene>> factory;

		// Shared pointer to the currently active scene
		std::shared_ptr<Scene> activeScene = nullptr;

	};

}

// A template registrar struct that, when constructed, preregisters the scene.
// Each unique scene type T creates a unique instantiation.
// SceneRegistrar assumes T has a default constructor or inherits the base constructor
namespace
{
	template<typename T>
	struct SceneRegistrar
	{
		SceneRegistrar(const std::string& name)
		{
			// Use base class constructor via std::make_shared<T>()
			Engine::SceneSystem::Preregister(std::static_pointer_cast<Engine::Scene>(
				std::make_shared<T>(name)
			));
		}
	};
}

// Macro to register a scene using templates. 
#define REGISTER_SCENE(SceneType) \
    namespace { \
        /* Inline variable ensures each TU gets its own instance without ODR issues */ \
        inline SceneRegistrar<SceneType> scene_registrar_instance_##SceneType(#SceneType); \
    }

// Macro to automatically derive and override all the methods in a scene for a header file, and then auto register it
// This macro is amazing, until we need to add more methods to a scene
#define DEFINE_SCENE(SceneType)                                  \
    class SceneType : public Engine::Scene                       \
    {                                                            \
    public:                                                      \
        using Engine::Scene::Scene; /* Inherit base constructors */ \
        int Awake() override;                                    \
        int Init() override;                                     \
        void Update(double dt) override;                         \
        void FixedUpdate(unsigned int tickThisSecond) override;  \
        int Exit() override;                                     \
    };                                                           \
    REGISTER_SCENE(SceneType); // then auto register it all in one big macro

