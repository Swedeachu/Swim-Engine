#pragma once

#include "Scene.h"
#include "SceneCatalog.h"
#include "SceneId.h"

#include "Engine/EngineState.h"
#include "Engine/Machine.h"
#include "Engine/Systems/Entity/BehaviorRegistry.h"

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Engine
{
	// Owns the loaded scenes, the scene/behaviour type catalogs and the active scene.
	//
	// Lifecycle of a scene: Awake once (when first activated), then Init each time it
	// becomes active (or is reloaded), Update/FixedUpdate while active, and Exit when it
	// is left (Exit destroys its entities, so the next Init starts from scratch).
	//
	// Engine state transitions are forwarded to the active scene (behaviour
	// OnPlay/OnPause/OnResume/OnStop, then Scene::OnStateChanged). Entering Stopped
	// resets the active scene to its initial state at the start of the next frame, so a
	// later Play starts fresh; the reset is deferred because a transition may be
	// requested from inside a behaviour or a UI callback.
	class SceneSystem : public Machine
	{
	  public:
		using SceneFactory = SceneCatalog::Factory;

		// Services shared with every scene. The behaviour registry is always this
		// system's own.
		void SetServices(SceneServices value);

		const SceneServices& GetServices() const { return services; }

		template <typename T> void RegisterSceneType(const std::string& name)
		{
			RegisterSceneType(name,
				[](const std::string& instanceName)
				{
					return std::static_pointer_cast<Scene>(std::make_shared<T>(instanceName));
				});
		}

		void RegisterSceneType(std::string name, SceneFactory factory);

		template <typename T> void RegisterBehaviorType(const std::string& name) { behaviorRegistry.Register<T>(name); }

		BehaviorRegistry& GetBehaviorRegistry() { return behaviorRegistry; }

		void SetStartupScene(std::string name) { startupSceneName = std::move(name); }

		const std::string& GetStartupScene() const { return startupSceneName; }

		// Adds an already-constructed scene instance under `name`.
		template <typename T, typename... Args> T& RegisterScene(const std::string& name, Args&&... args)
		{
			if (scenes.contains(name))
			{
				throw std::runtime_error("Scene instance with name '" + name + "' is already registered.");
			}
			auto scene = std::make_shared<T>(std::forward<Args>(args)...);
			T& ref = *scene;
			AddLoaded(name, std::move(scene));
			return ref;
		}

		int Awake() override;
		int Init() override;
		// Applies deferred scene switches/resets and starts transform tracking.
		void BeginFrame();
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		// Engine state transition (from the EngineStateMachine).
		void OnEngineStateChanged(EngineState previous, EngineState current);

		// Switches immediately: exits the current scene and Awakes (first time) / Inits
		// the new one. Do not call from inside a scene update; use RequestScene.
		void SetScene(const std::string& name);

		// Switches at the start of the next frame.
		void RequestScene(std::string name) { pendingScene = std::move(name); }

		// Resets the active scene (Exit + Init) at the start of the next frame.
		void RequestReload() { pendingReload = true; }

		void ReloadActiveScene();

		std::shared_ptr<Scene>& GetActiveScene() { return activeScene; }

		const std::shared_ptr<Scene>& GetActiveScene() const { return activeScene; }

		SceneId GetActiveSceneId() const { return activeSceneId; }

		std::string GetActiveSceneName() const;
		SceneId FindSceneId(std::string_view name) const;
		std::vector<std::string> GetSceneNames() const;

		std::uint64_t GetReloadCount() const { return reloadCount; }

		bool DispatchCommand(std::string_view command) const { return services.DispatchCommand && services.DispatchCommand(command); }

	  private:
		struct LoadedScene
		{
			SceneId Id;
			std::shared_ptr<Scene> Instance;
			bool Awakened = false;
		};

		void AddLoaded(const std::string& name, std::shared_ptr<Scene> scene);
		void InjectServices(Scene& scene);
		int ActivateLoaded(LoadedScene& loaded, const std::string& name);
		void ExitActive();

		std::map<std::string, LoadedScene> scenes;
		SceneCatalog sceneCatalog;
		BehaviorRegistry behaviorRegistry;
		std::string startupSceneName;

		std::shared_ptr<Scene> activeScene = nullptr;
		SceneId activeSceneId{};
		std::uint64_t nextSceneId = 1;
		std::uint64_t reloadCount = 0;

		std::string pendingScene;
		bool pendingReload = false;
		bool awake = false;

		SceneServices services{};
	};
} // namespace Engine
