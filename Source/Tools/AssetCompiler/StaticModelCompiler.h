#pragma once

#include "Engine/Assets/ContentHash.h"
#include "Engine/Assets/SassetFormat.h"
#include "Tools/AssetCompiler/IntermediateModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::AssetCompiler
{

	enum class StaticModelCompileErrorCode : std::uint8_t
	{
		None,
		InvalidSourcePath,
		InvalidSourceData,
		UnsupportedTopology,
		UnsupportedTextureSource,
		Ktx2CompileFailed,
		SassetBuildFailed,
		Overflow
	};

	struct StaticModelCompileError
	{
		StaticModelCompileErrorCode Code = StaticModelCompileErrorCode::None;
		std::string Message;
	};

	struct CompiledSasset
	{
		Swim::Assets::AssetId Id{};
		Swim::Assets::SassetAssetType Type = Swim::Assets::SassetAssetType::Unknown;
		std::string LogicalPath;
		std::vector<std::byte> Bytes;
		bool IsRoot = false;
	};

	struct StaticModelCompileStats
	{
		std::size_t Meshes = 0;
		std::size_t Materials = 0;
		std::size_t Textures = 0;
		std::size_t Samplers = 0;
	};

	struct StaticModelCompileResult
	{
		Swim::Assets::AssetId RootId{};
		std::string RootLogicalPath;
		std::vector<CompiledSasset> Assets;
		StaticModelCompileStats Stats;
		StaticModelCompileError Error;

		explicit operator bool() const
		{
			return Error.Code == StaticModelCompileErrorCode::None;
		}
	};

	Swim::Assets::ContentHash GetStaticModelCompilerProfileHash();

	class StaticModelCompiler
	{
	public:
		StaticModelCompileResult Compile(
			const IntermediateModel& model,
			std::string_view sourceLogicalPath,
			std::vector<Swim::Assets::SassetSourceDependency> sourceDependencies) const;
	};

}
