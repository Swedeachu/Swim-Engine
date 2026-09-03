#include "Engine/Assets/AssetSystem.h"
#include "Engine/EngineConfig.h"
#include "Engine/Jobs/JobSystem.h"

#include <iostream>

namespace
{
	struct HeadlessAsset
	{
		int Value = 0;
	};
}

int main()
{
	Engine::EngineConfig config{};
	Swim::Jobs::JobSystem jobs;
	if (!jobs.Initialize())
	{
		std::cerr << "Headless foundation: JobSystem initialization failed.\n";
		return 1;
	}

	Swim::Assets::AssetSystem assets;
	if (!assets.Initialize())
	{
		std::cerr << "Headless foundation: AssetSystem initialization failed.\n";
		jobs.Shutdown();
		return 1;
	}

	auto handle = assets.Declare<HeadlessAsset>("Tests/Headless.asset");
	assets.Publish(handle, HeadlessAsset{ 42 }, Swim::Assets::ComputeContentHash("headless"));
	const HeadlessAsset* asset = assets.Resolve(handle);
	const bool valid = asset && asset->Value == 42 && config.Window.Width > 0;

	assets.Shutdown();
	jobs.Shutdown();
	return valid ? 0 : 1;
}
