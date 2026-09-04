#include "PCH.h"
#include "Engine/Logging/Log.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Game/Scenes/SandBox.h"
#include "Game/Behaviors/Demo/Spin.h"
#include "Game/Behaviors/Demo/SimpleMovement.h"
#include "Game/Behaviors/Phys/BallShooter.h"

namespace
{
	class LoggingLifetime final
	{
	public:
		LoggingLifetime()
		{
			Engine::Logging::Initialize();
		}

		~LoggingLifetime()
		{
			Engine::Logging::Shutdown();
		}
	};
}

int main(int argc, char** argv)
{
	LoggingLifetime loggingLifetime;

	try
	{
		auto parsedConfig = Engine::SwimEngine::ParseStartingEngineArgs(argc, argv);
		if (!parsedConfig)
		{
			for (const std::string& error : parsedConfig.Errors)
			{
				std::cerr << "[Engine] " << error << '\n';
			}
			return -1;
		}

		Engine::SwimEngine engine(std::move(parsedConfig.Config));
		Engine::SceneSystem* scenes = engine.GetSceneSystem();
		scenes->RegisterBehaviorType<Game::Spin>("Spin");
		scenes->RegisterBehaviorType<Game::SimpleMovement>("SimpleMovement");
		scenes->RegisterBehaviorType<Game::BallShooter>("BallShooter");
		scenes->RegisterSceneType<Game::SandBox>("SandBox");
		scenes->SetStartupScene("SandBox");

		if (engine.Start() == 0)
		{
			return engine.Run();
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "[Engine] Unhandled startup/runtime exception: " << error.what() << '\n';
		return -1;
	}
	catch (...)
	{
		std::cerr << "[Engine] Unhandled non-standard startup/runtime exception.\n";
		return -1;
	}

	return -1;
}
