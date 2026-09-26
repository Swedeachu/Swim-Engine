#pragma once

#include "Engine/Commands/CommandRegistry.h"
#include "Engine/EngineConfig.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/SwimEngine.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Tests
{
	// A real SwimEngine without a GPU (--headless --no-render) at a fixed 60 Hz frame
	// delta: platform, jobs, IO, assets, input, commands, state machine, clock, physics,
	// scene system and the UI runtime all run; only the renderer is absent. Scenes are
	// registered through `registerScenes` before Start.
	class HeadlessEngine
	{
	  public:
		explicit HeadlessEngine(const std::function<void(Engine::SceneSystem&)>& registerScenes, std::string startupScene = {},
			Engine::EngineState initialState = Engine::EngineState::Playing, std::vector<std::string> startupCommands = {})
		{
			Engine::EngineConfig config;
			config.Present = Engine::PresentMode::Headless;
			config.Render = false;
			config.Validation = false;
			config.FixedFrameDelta = 1.0 / 60.0;
			config.FixedRate = 60.0;
			config.InitialState = initialState;
			config.StartupScene = std::move(startupScene);
			config.StartupCommands = std::move(startupCommands);
			engine = std::make_unique<Engine::SwimEngine>(config);
			registerScenes(*engine->GetSceneSystem());
			startResult = engine->Start();
		}

		~HeadlessEngine() { engine.reset(); }

		bool Started() const { return startResult == 0; }

		Engine::SwimEngine& operator*() { return *engine; }

		Engine::SwimEngine* operator->() { return engine.get(); }

		// Runs `frames` frames; false if the engine stopped early.
		bool Tick(std::uint32_t frames = 1)
		{
			for (std::uint32_t i = 0; i < frames; ++i)
			{
				if (!engine->Tick())
				{
					return false;
				}
			}
			return true;
		}

		bool Command(std::string_view command) { return engine->GetCommandRegistry()->ParseAndDispatch(command); }

		Engine::Scene& Scene() { return *engine->GetSceneSystem()->GetActiveScene(); }

		template <typename T> T* SceneAs() { return dynamic_cast<T*>(engine->GetSceneSystem()->GetActiveScene().get()); }

	  private:
		std::unique_ptr<Engine::SwimEngine> engine;
		int startResult = -1;
	};
} // namespace Swim::Tests
