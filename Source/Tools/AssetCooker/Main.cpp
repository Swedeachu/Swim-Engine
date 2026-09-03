#include "Engine/Assets/AssetSystem.h"
#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
	const std::filesystem::path assetRoot = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("Assets");

	Swim::Assets::AssetSystem assets;
	if (!assets.Initialize())
	{
		std::cerr << "[Swim Asset Cooker] Failed to initialize AssetSystem.\n";
		return 1;
	}

	const Swim::AssetCompiler::DevelopmentAssetBootstrapResult result =
		Swim::AssetCompiler::RunDevelopmentAssetBootstrap(assetRoot, assets);

	std::cout << "[Swim Asset Cooker] Sources: " << result.Stats.SourcesDiscovered
		<< ", current: " << result.Stats.SourcesCurrent
		<< ", cooked: " << result.Stats.SourcesCooked
		<< ", root models: " << result.Stats.RootModelsLoaded
		<< ", loaded .sasset files: " << result.Stats.SassetsLoaded << ".\n";

	for (const auto& error : result.Errors)
	{
		std::cerr << "[Swim Asset Cooker] " << error.SourcePath.string() << ": " << error.Message << '\n';
	}

	assets.Shutdown();
	return result.Succeeded() ? 0 : 1;
}
