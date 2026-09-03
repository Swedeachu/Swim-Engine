#pragma once

#include "Engine/Assets/AssetId.h"
#include "Engine/Assets/ContentHash.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Swim::Assets
{

	class AssetSystem;

	inline constexpr std::uint32_t SassetSchemaVersion = 1;
	inline constexpr std::uint32_t SassetPayloadVersion = 1;
	inline constexpr std::size_t SassetHeaderSize = 160;
	inline constexpr std::size_t SassetChunkEntrySize = 72;

	enum class SassetAssetType : std::uint32_t
	{
		Unknown = 0,
		Mesh = 1,
		Texture = 2,
		Sampler = 3,
		MaterialTemplate = 4,
		MaterialInstance = 5,
		Model = 6
	};

	enum class SassetChunkType : std::uint32_t
	{
		LogicalPath = 1,
		SourceProvenance = 2,
		AssetPayload = 3
	};

	enum class SassetCompression : std::uint32_t
	{
		None = 0,
		Zstandard = 1
	};

	enum class SassetErrorCode : std::uint8_t
	{
		None,
		Truncated,
		InvalidMagic,
		UnsupportedVersion,
		InvalidAssetType,
		InvalidAssetId,
		InvalidTable,
		InvalidChunk,
		UnsupportedCompression,
		HashMismatch,
		InvalidPayload,
		PathConflict,
		PublishFailed
	};

	struct SassetError
	{
		SassetErrorCode Code = SassetErrorCode::None;
		std::string Message;
	};

	struct SassetSourceDependency
	{
		std::string LogicalPath;
		ContentHash Hash{};
	};

	struct SassetChunkDesc
	{
		SassetChunkType Type = SassetChunkType::AssetPayload;
		SassetCompression Compression = SassetCompression::None;
		std::uint64_t OffsetBytes = 0;
		std::uint64_t SizeBytes = 0;
		std::uint64_t UncompressedSizeBytes = 0;
		std::uint32_t Alignment = 1;
		ContentHash Hash{};
	};

	struct SassetMetadata
	{
		std::uint32_t SchemaVersion = 0;
		SassetAssetType Type = SassetAssetType::Unknown;
		AssetId Id{};
		ContentHash ContentHashValue{};
		ContentHash CompilerProfileHash{};
		ContentHash SourceHash{};
		std::vector<AssetId> Dependencies;
		std::vector<SassetChunkDesc> Chunks;
		std::string LogicalPath;
		std::vector<SassetSourceDependency> SourceDependencies;
	};

	struct SassetParseResult
	{
		SassetMetadata Metadata;
		SassetError Error;

		explicit operator bool() const
		{
			return Error.Code == SassetErrorCode::None;
		}
	};

	struct SassetLoadResult
	{
		AssetId Id{};
		SassetAssetType Type = SassetAssetType::Unknown;
		SassetError Error;

		explicit operator bool() const
		{
			return Error.Code == SassetErrorCode::None;
		}
	};

	SassetParseResult ParseSasset(std::span<const std::byte> bytes, bool validateChunkHashes = true);
	SassetLoadResult LoadSasset(AssetSystem& assets, std::span<const std::byte> bytes);
	std::span<const std::byte> GetSassetChunkBytes(
		std::span<const std::byte> bytes,
		const SassetMetadata& metadata,
		SassetChunkType type);

}
