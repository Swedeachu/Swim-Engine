#include "PCH.h"
#include "Engine/SwimEngine.h"

// this makes it so no console appears in a release build
#ifndef _SWIM_DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main(int argc, char** argv)
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
  if (engine.Start() == 0)
  {
    return engine.Run();
  }

  return -1;
}
