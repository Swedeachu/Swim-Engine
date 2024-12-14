#include "PCH.h"
#include "Engine/SwimEngine.h"

// this makes it so no console appears in a release build
#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main()
{
	auto engine = std::make_shared<Engine::SwimEngine>();
	if (engine->Start() == 0) return engine->Run();

	return -1; 
}
