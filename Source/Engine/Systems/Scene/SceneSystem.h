#pragma once

#include "Scene.h"
#include "SceneId.h"
#include "SceneCatalog.h"
#include "Engine/Commands/CommandRegistry.h"
#include "Engine/EngineState.h"
#include "Engine/Systems/Entity/BehaviorRegistry.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <stdexcept>
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

	struct SceneCoreServices
	{
		Swim::Platform::FileSystem* Files = nullptr;
		Swim::Jobs::JobSystem* Jobs = nullptr;
		Swim::IO::AsyncIoService* IO = nullptr;
		Swim::Assets::AssetSystem* Assets = nullptr;
		Swim::Memory::FrameArena* FrameMemory = nullptr;
		const EngineState* State = nullptr;

		bool IsValid() const
		{
			return Files && Jobs && IO && Assets && FrameMemory && State;
		}
	};

	struct ScenePresentationServices
	{
		Swim::Input::InputSystem* Input = nullptr;
		CameraSystem* Camera = nullptr;
		CubeMapController* CubeMap = nullptr;
		MeshPool* Meshes = nullptr;
		TexturePool* Textures = nullptr;
		MaterialPool* Materials = nullptr;
		FontPool* Fonts = nullptr;

		bool IsAvailable() const
		{
			return Input && Camera && Meshes && Textures && Materials && Fonts;
		}
	};

	struct SceneToolServices
	{
		Swim::Commands::CommandRegistry* Commands = nullptr;
		std::function<int()> GetFPS;
	};

	struct SceneSystemServices
	{
		SceneCoreServices Core{};
		ScenePresentationServices Presentation{};
		SceneToolServices Tools{};

		bool IsValid() const { return Core.IsValid(); }
		bool HasPresentation() const { return Presentation.IsAvailable(); }
	};

	class SceneSystem : public Machine
	{

	public:

		using SceneFactory = SceneCatalog::Factory;

		void SetServices(SceneSystemServices services) { this->services = std::move(services); }
		void SetCubeMapController(CubeMapController* cubeMap) { services.Presentation.CubeMap = cubeMap; }

		template <typename T>
		void RegisterSceneType(const std::string& name)
		{
			RegisterSceneType(name, [](const std::string& instanceName)
			{
				return std::static_pointer_cast<Scene>(std::make_shared<T>(instanceName));
			});
		}

		void RegisterSceneType(std::string name, SceneFactory factory);

		template <typename T>
		void RegisterBehaviorType(const std::string& name)
		{
			behaviorRegistry.Register<T>(name);
		}

		void SetStartupScene(std::string name) { startupSceneName = std::move(name); }

		int Awake() override;

		int Init() override;

		void BeginFrame();

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) override;

		int Exit() override;

		template <typename T, typename... Args>
		void RegisterScene(const std::string& name, Args&&... args)
		{
			if (scenes.contains(name))
			{
				throw std::runtime_error("Scene instance with name '" + name + "' is already registered.");
			}

			std::shared_ptr<Scene> scene = std::make_shared<T>(std::forward<Args>(args)...);
			if (services.IsValid())
			{
				InjectServices(*scene);
			}
			SceneId id(nextSceneId++);
			scenes.emplace(name, LoadedScene{ id, std::move(scene) });
		}

		// Sets the active scene by name, optionally exiting the current one
		void SetScene(const std::string& name, bool exitCurrent = true, bool initNew = true, bool awakeNew = false);

		std::shared_ptr<Scene>& GetActiveScene() { return activeScene; }
		const std::shared_ptr<Scene>& GetActiveScene() const { return activeScene; }
		SceneId GetActiveSceneId() const { return activeSceneId; }
		SceneId FindSceneId(std::string_view name) const;
		bool DispatchCommand(std::string_view command);

	private:

		void InjectServices(Scene& scene);



		struct LoadedScene
		{
			SceneId Id;
			std::shared_ptr<Scene> Instance;
		};

		// Loaded scene instances are owned by this SceneSystem and receive a runtime SceneId.
		std::map<std::string, LoadedScene> scenes;

		// Scene type construction metadata is owned by this SceneSystem instance.
		// Game/application code registers descriptors explicitly before Awake().
		SceneCatalog sceneCatalog;
		BehaviorRegistry behaviorRegistry;
		std::string startupSceneName;

		// Shared pointer to the application-designated active scene.
		std::shared_ptr<Scene> activeScene = nullptr;
		SceneId activeSceneId{};
		std::uint64_t nextSceneId = 1;

		SceneSystemServices services{};

	};

}
