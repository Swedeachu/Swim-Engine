#pragma once

#include "Scene.h"
#include "Engine/Systems/IO/CommandSystem.h"
#include "Engine/EngineState.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <string_view>
#include <utility>

namespace Swim::Platform
{
	class FileSystem;
}

namespace Swim::Jobs
{
	class JobSystem;
}

namespace Engine
{

	struct SceneSystemServices
	{
		InputManager* Input = nullptr;
		CommandSystem* Commands = nullptr;
		CameraSystem* Camera = nullptr;
		VulkanRenderer* Vulkan = nullptr;
		OpenGLRenderer* OpenGL = nullptr;
		MeshPool* Meshes = nullptr;
		TexturePool* Textures = nullptr;
		MaterialPool* Materials = nullptr;
		FontPool* Fonts = nullptr;
		Swim::Platform::FileSystem* Files = nullptr;
		Swim::Jobs::JobSystem* Jobs = nullptr;
		const EngineState* State = nullptr;
		ClipSpaceDepthRange ClipDepth = ClipSpaceDepthRange::ZeroToOne;
		std::function<bool(const std::string&, std::uintptr_t)> SendEditorMessage;
		std::function<int()> GetFPS;

		bool IsValid() const
		{
			return Input && Commands && Camera && State && Files && Jobs && Meshes && Textures && Materials && Fonts && (Vulkan || OpenGL);
		}
	};

	class SceneSystem : public Machine
	{

	public:

		using SceneFactory = std::function<std::shared_ptr<Scene>()>;

		static void Preregister(std::string name, SceneFactory factory);

		void SetServices(SceneSystemServices services) { this->services = std::move(services); }

		int Awake() override;

		int Init() override;

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) override;

		int Exit() override;

		template <typename T, typename... Args>
		void RegisterScene(const std::string& name, Args&&... args)
		{
			std::shared_ptr<Scene> scene = std::make_shared<T>(std::forward<Args>(args)...);
			if (services.IsValid())
			{
				InjectServices(*scene);
			}
			scenes[name] = std::move(scene);
		}

		// Sets the active scene by name, optionally exiting the current one
		void SetScene(const std::string& name, bool exitCurrent = true, bool initNew = true, bool awakeNew = false);

		std::shared_ptr<Scene>& GetActiveScene() { return activeScene; }
		bool DispatchCommand(std::string_view command);
		bool SendEditorMessage(const std::string& message, std::uintptr_t channel = 1) const
		{
			return services.SendEditorMessage && services.SendEditorMessage(message, channel);
		}

	private:

		void RegisterEditorCommands();
		void SendBehaviorsToEditor();
		void InjectServices(Scene& scene);

		// Per-command registration functions
		void RegisterEntityCreateCommand(CommandSystem& cmd);
		void RegisterEntityDestroyCommand(CommandSystem& cmd);
		void RegisterEntityAddComponentCommand(CommandSystem& cmd);
		void RegisterEntityRemoveComponentCommand(CommandSystem& cmd);
		void RegisterEntitySetMaterialCommand(CommandSystem& cmd);		
		void RegisterEntityBehaviorAddCommand(CommandSystem& cmd);
		void RegisterEntityBehaviorRemoveCommand(CommandSystem& cmd);

		// Small helpers used by the add/remove component commands
		void AddComponentByName(Scene& scene, unsigned int entityId, const std::string& componentName);
		void RemoveComponentByName(Scene& scene, unsigned int entityId, const std::string& componentName);

		// Map of scenes by name
		std::map<std::string, std::shared_ptr<Scene>> scenes;

		struct PreregisteredScene
		{
			std::string Name;
			SceneFactory Factory;
		};

		// Static registration contains constructors only; runtime Scene instances are owned per SceneSystem.
		static std::vector<PreregisteredScene> factory;

		// Shared pointer to the currently active scene
		std::shared_ptr<Scene> activeScene = nullptr;

		SceneSystemServices services{};

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
			Engine::SceneSystem::Preregister(name, [name]()
			{
				return std::static_pointer_cast<Engine::Scene>(std::make_shared<T>(name));
			});
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

