#include "Engine/Platform/PlatformSystem.h"
#include <iostream>

int main()
{
	Swim::Platform::PlatformDesc desc{};
	desc.Headless = true;
	desc.ApplicationName = "Swim Headless Platform Smoke";

	Swim::Platform::PlatformSystem platform;
	if (!platform.Initialize(desc))
	{
		return 1;
	}

	const auto& fileSystem = platform.GetFileSystem();
	if (fileSystem.GetExecutableDirectory().empty() || fileSystem.GetCacheRoot().empty())
	{
		return 2;
	}

	std::cout << "Headless platform initialized.\n";
	return 0;
}
