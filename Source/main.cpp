#include "Engine/Logging/Log.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/SwimEngine.h"
#include "Game/Game.h"

#include <exception>
#include <iostream>

namespace
{
	class LoggingLifetime final
	{
	  public:
		LoggingLifetime() { Engine::Logging::Initialize(); }

		~LoggingLifetime() { Engine::Logging::Shutdown(); }
	};
} // namespace

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
			std::cerr << Engine::GetEngineConfigUsage();
			return -1;
		}
		if (parsedConfig.Config.ShowHelp)
		{
			std::cout << Engine::GetEngineConfigUsage();
			return 0;
		}

		Engine::SwimEngine engine(std::move(parsedConfig.Config));
		Game::Register(*engine.GetSceneSystem());

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
