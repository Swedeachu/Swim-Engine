#include "PCH.h"
#include "Engine/SwimEngine.h"

// this makes it so no console appears in a release build
#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main(int argc, char** argv)
{
  HWND parentHwnd = nullptr;

  for (int i = 1; i < argc; ++i)
  {
    if (std::string(argv[i]) == "--parent-hwnd" && i + 1 < argc)
    {
      uint64_t val = std::strtoull(argv[++i], nullptr, 10);
      parentHwnd = reinterpret_cast<HWND>(val);
    }
  }

	auto engine = std::make_shared<Engine::SwimEngine>(parentHwnd);
	if (engine->Start() == 0) return engine->Run();

	return -1; 
}
