#pragma once

#include "Engine/Assets/AssetId.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Swim::Assets
{
	class AssetSystem;
}

namespace Swim::AssetCompiler
{

	enum class DevelopmentAssetErrorStage : unsigned char
	{
		Inspect,
		Import,
		Optimize,
		Compile,
		Publish,
		Load
	};

	struct DevelopmentAssetError
	{
		DevelopmentAssetErrorStage Stage = DevelopmentAssetErrorStage::Inspect;
		std::filesystem::path SourcePath;
		std::string Message;
	};

	struct DevelopmentAssetBootstrapStats
	{
		std::size_t SourcesDiscovered = 0;
		std::size_t SourcesCurrent = 0;
		std::size_t SourcesCooked = 0;
		std::size_t SourcesSkippedUnsupported = 0;
		std::size_t RootModelsLoaded = 0;
		std::size_t SassetsLoaded = 0;
	};

	struct DevelopmentAssetBootstrapResult
	{
		DevelopmentAssetBootstrapStats Stats;
		std::vector<DevelopmentAssetError> Errors;
		std::vector<Swim::Assets::AssetId> RootModels;

		bool Succeeded() const { return Errors.empty(); }
	};

	DevelopmentAssetBootstrapResult RunDevelopmentAssetBootstrap(
		const std::filesystem::path& assetRoot,
		Swim::Assets::AssetSystem& assets);

}
