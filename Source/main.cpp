#include "PCH.h"
#include "Engine/SwimEngine.h"

// this makes it so no console appears in a release build
#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main(int argc, char** argv)
{
  auto engine = std::make_shared<Engine::SwimEngine>(Engine::SwimEngine::ParseStartingEngineArgs(argc, argv));
  if (engine->Start() == 0) return engine->Run(); // runs if started with zero errors

  return -1;
}
