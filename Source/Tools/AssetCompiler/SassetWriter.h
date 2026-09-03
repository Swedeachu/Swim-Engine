#pragma once

#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Engine/Assets/TextureAsset.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace Swim::AssetCompiler
{

	struct SassetBuildInput
	{
		Swim::Assets::SassetAssetType Type = Swim::Assets::SassetAssetType::Unknown;
		Swim::Assets::AssetId Id{};
		std::string LogicalPath;
		Swim::Assets::ContentHash CompilerProfileHash{};
		Swim::Assets::ContentHash SourceHash{};
		std::vector<Swim::Assets::AssetId> Dependencies;
		std::vector<Swim::Assets::SassetSourceDependency> SourceDependencies;
		std::vector<std::byte> Payload;
	};

	struct SassetBuildResult
	{
		std::vector<std::byte> Bytes;
		Swim::Assets::SassetError Error;

		explicit operator bool() const
		{
			return Error.Code == Swim::Assets::SassetErrorCode::None;
		}
	};

	SassetBuildResult BuildSasset(const SassetBuildInput& input);
	Swim::Assets::ContentHash ComputeSourceGraphHash(std::span<const Swim::Assets::SassetSourceDependency> dependencies);

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::MeshAsset& asset);
	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::TextureAsset& asset);
	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::SamplerAsset& asset);
	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::MaterialTemplateAsset& asset);
	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::MaterialInstanceAsset& asset);
	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::ModelAsset& asset);

}
