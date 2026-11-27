#include "Engine/Assets/SassetFormat.h"
#include "Engine/Assets/SassetPayloadReaders.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/SassetDecodedAsset.h"
#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SkeletonAsset.h"
#include "Engine/Assets/TextureAsset.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace Swim::Assets
{

	using namespace SassetDetail;

	namespace
	{

		constexpr std::array<std::byte, 8> SassetMagic{ std::byte{ 'S' }, std::byte{ 'A' }, std::byte{ 'S' }, std::byte{ 'S' },
			std::byte{ 'E' }, std::byte{ 'T' }, std::byte{ 0x0D }, std::byte{ 0x0A } };
		constexpr std::size_t HeaderSize = SassetHeaderSize;
		constexpr std::size_t ChunkEntrySize = SassetChunkEntrySize;
		constexpr std::size_t ChunkHashOffset = 40;

		SassetParseResult MakeParseError(SassetErrorCode code, const char* message)
		{
			SassetParseResult result;
			result.Error.Code = code;
			result.Error.Message = message;
			return result;
		}

		SassetLoadResult MakeLoadError(SassetErrorCode code, const char* message)
		{
			SassetLoadResult result;
			result.Error.Code = code;
			result.Error.Message = message;
			return result;
		}

		bool RangeFits(std::uint64_t offset, std::uint64_t size, std::size_t totalSize)
		{
			if (offset > static_cast<std::uint64_t>(totalSize))
			{
				return false;
			}

			return size <= static_cast<std::uint64_t>(totalSize) - offset;
		}

		ContentHash ReadHash(std::span<const std::byte> bytes, std::size_t offset)
		{
			ContentHash hash;

			for (std::size_t index = 0; index < hash.Bytes.size(); ++index)
			{
				hash.Bytes[index] = static_cast<std::uint8_t>(bytes[offset + index]);
			}

			return hash;
		}

		bool IsKnownType(SassetAssetType type)
		{
			switch (type)
			{
			case SassetAssetType::Mesh:
			case SassetAssetType::Texture:
			case SassetAssetType::Sampler:
			case SassetAssetType::MaterialTemplate:
			case SassetAssetType::MaterialInstance:
			case SassetAssetType::Model:
			case SassetAssetType::Skeleton:
			case SassetAssetType::AnimationClip:
				return true;
			default:
				return false;
			}
		}

		template <typename T, typename ReaderFn>
		bool PublishDecoded(AssetSystem& assets, const SassetMetadata& metadata, BinaryReader& reader, ReaderFn&& read)
		{
			AssetHandle<T> handle = assets.Declare<T>(metadata.Id);
			assets.BeginLoading(handle);
			T asset{};

			if (!read(reader, asset))
			{
				assets.Fail(handle, AssetError{ AssetErrorCode::InvalidData, "invalid .sasset payload" });
				return false;
			}

			return assets.Publish(handle, std::move(asset), metadata.ContentHashValue, metadata.Dependencies);
		}

	} // namespace

	std::span<const std::byte> GetSassetChunkBytes(std::span<const std::byte> bytes, const SassetMetadata& metadata, SassetChunkType type)
	{
		for (const SassetChunkDesc& chunk : metadata.Chunks)
		{
			if (chunk.Type != type || chunk.Compression != SassetCompression::None)
			{
				continue;
			}

			if (!RangeFits(chunk.OffsetBytes, chunk.SizeBytes, bytes.size()))
			{
				return {};
			}

			return bytes.subspan(static_cast<std::size_t>(chunk.OffsetBytes), static_cast<std::size_t>(chunk.SizeBytes));
		}

		return {};
	}

	namespace
	{

		// The header's table locations (validated against the payload by ReadHeader).
		struct SassetTables
		{
			std::uint64_t DependencyOffset = 0;
			std::uint32_t DependencyCount = 0;
			std::uint32_t ChunkCount = 0;
			std::uint64_t ChunkOffset = 0;
		};

		// The fixed header after the magic: metadata fields and table locations, validated.
		std::optional<SassetParseResult> ReadHeader(std::span<const std::byte> bytes, SassetMetadata& metadata, SassetTables& tables)
		{
			metadata.SchemaVersion = ReadU32(bytes, 8);
			const std::uint32_t headerSize = ReadU32(bytes, 12);
			metadata.Type = static_cast<SassetAssetType>(ReadU32(bytes, 16));
			metadata.Id = AssetId{ ReadU64(bytes, 24) };
			metadata.ContentHashValue = ReadHash(bytes, 32);
			metadata.CompilerProfileHash = ReadHash(bytes, 64);
			metadata.SourceHash = ReadHash(bytes, 96);
			tables.DependencyOffset = ReadU64(bytes, 128);
			tables.DependencyCount = ReadU32(bytes, 136);
			tables.ChunkCount = ReadU32(bytes, 140);
			tables.ChunkOffset = ReadU64(bytes, 144);
			const std::uint64_t declaredFileSize = ReadU64(bytes, 152);

			if (metadata.SchemaVersion != SassetSchemaVersion || headerSize != HeaderSize)
			{
				return MakeParseError(SassetErrorCode::UnsupportedVersion, "unsupported .sasset schema/header version");
			}

			if (!IsKnownType(metadata.Type))
			{
				return MakeParseError(SassetErrorCode::InvalidAssetType, ".sasset declares an unknown asset type");
			}

			if (!metadata.Id.IsValid())
			{
				return MakeParseError(SassetErrorCode::InvalidAssetId, ".sasset declares an invalid AssetId");
			}

			if (declaredFileSize != bytes.size())
			{
				return MakeParseError(SassetErrorCode::InvalidTable, ".sasset declared file size does not match payload size");
			}

			if (tables.DependencyCount > MaxCollectionCount || tables.ChunkCount > MaxCollectionCount)
			{
				return MakeParseError(SassetErrorCode::InvalidTable, ".sasset table count exceeds runtime limits");
			}

			if (!RangeFits(tables.DependencyOffset, static_cast<std::uint64_t>(tables.DependencyCount) * 8u, bytes.size()))
			{
				return MakeParseError(SassetErrorCode::InvalidTable, ".sasset dependency table is out of range");
			}

			if (!RangeFits(tables.ChunkOffset, static_cast<std::uint64_t>(tables.ChunkCount) * ChunkEntrySize, bytes.size()))
			{
				return MakeParseError(SassetErrorCode::InvalidTable, ".sasset chunk table is out of range");
			}

			return std::nullopt;
		}

		// Which singleton chunks the table held so far.
		struct FoundChunks
		{
			bool LogicalPath = false;
			bool Payload = false;
			bool Provenance = false;
		};

		// One chunk-table entry at `offset`, validated; an error result when it is invalid.
		std::optional<SassetParseResult> ReadChunk(
			std::span<const std::byte> bytes, std::size_t offset, bool validateChunkHashes, FoundChunks& found, SassetChunkDesc& chunk)
		{
			chunk.Type = static_cast<SassetChunkType>(ReadU32(bytes, offset + 0));
			chunk.Compression = static_cast<SassetCompression>(ReadU32(bytes, offset + 4));
			chunk.OffsetBytes = ReadU64(bytes, offset + 8);
			chunk.SizeBytes = ReadU64(bytes, offset + 16);
			chunk.UncompressedSizeBytes = ReadU64(bytes, offset + 24);
			chunk.Alignment = ReadU32(bytes, offset + 32);
			chunk.Hash = ReadHash(bytes, offset + ChunkHashOffset);

			if (chunk.Alignment == 0 || (chunk.Alignment & (chunk.Alignment - 1)) != 0)
			{
				return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset chunk alignment is not a non-zero power of two");
			}

			if ((chunk.OffsetBytes % chunk.Alignment) != 0 || !RangeFits(chunk.OffsetBytes, chunk.SizeBytes, bytes.size()))
			{
				return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset chunk is out of range or misaligned");
			}

			if (chunk.Compression != SassetCompression::None)
			{
				return MakeParseError(
					SassetErrorCode::UnsupportedCompression, ".sasset v1 runtime currently accepts uncompressed chunks only");
			}

			if (chunk.UncompressedSizeBytes != chunk.SizeBytes)
			{
				return MakeParseError(SassetErrorCode::InvalidChunk, "uncompressed .sasset chunk has mismatched size fields");
			}

			if (validateChunkHashes)
			{
				const auto chunkBytes =
					bytes.subspan(static_cast<std::size_t>(chunk.OffsetBytes), static_cast<std::size_t>(chunk.SizeBytes));

				if (ComputeContentHash(chunkBytes) != chunk.Hash)
				{
					return MakeParseError(SassetErrorCode::HashMismatch, ".sasset chunk content hash mismatch");
				}
			}

			switch (chunk.Type)
			{
			case SassetChunkType::LogicalPath:

				if (found.LogicalPath)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains duplicate logical-path chunks");
				}

				found.LogicalPath = true;
				break;
			case SassetChunkType::SourceProvenance:

				if (found.Provenance)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains duplicate source-provenance chunks");
				}

				found.Provenance = true;
				break;
			case SassetChunkType::AssetPayload:

				if (found.Payload)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains duplicate asset-payload chunks");
				}

				found.Payload = true;
				break;
			default:
				return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains an unknown required chunk type");
			}

			return std::nullopt;
		}

		std::optional<SassetParseResult> ReadLogicalPath(std::span<const std::byte> bytes, SassetMetadata& metadata)
		{
			const std::span<const std::byte> logicalPathBytes = GetSassetChunkBytes(bytes, metadata, SassetChunkType::LogicalPath);

			if (logicalPathBytes.empty())
			{
				return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset logical path cannot be empty");
			}

			metadata.LogicalPath.assign(reinterpret_cast<const char*>(logicalPathBytes.data()), logicalPathBytes.size());

			try
			{
				if (NormalizeAssetPath(metadata.LogicalPath) != metadata.LogicalPath)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset logical path is not canonical");
				}
			}
			catch (const std::exception&)
			{
				return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset logical path is invalid");
			}
			return std::nullopt;
		}

		std::optional<SassetParseResult> ReadSourceProvenance(std::span<const std::byte> bytes, SassetMetadata& metadata)
		{
			BinaryReader reader(GetSassetChunkBytes(bytes, metadata, SassetChunkType::SourceProvenance));

			if (reader.U32() != SassetPayloadVersion)
			{
				return MakeParseError(SassetErrorCode::InvalidPayload, ".sasset source provenance version is unsupported");
			}

			std::uint32_t count = 0;

			if (!reader.Count(count))
			{
				return MakeParseError(SassetErrorCode::InvalidPayload, ".sasset source provenance count is invalid");
			}

			metadata.SourceDependencies.reserve(count);

			for (std::uint32_t index = 0; index < count; ++index)
			{
				SassetSourceDependency dependency;
				dependency.LogicalPath = reader.String();

				for (std::uint8_t& byte : dependency.Hash.Bytes)
				{
					byte = reader.U8();
				}

				try
				{
					dependency.LogicalPath = NormalizeAssetPath(dependency.LogicalPath);
				}
				catch (const std::exception&)
				{
					return MakeParseError(SassetErrorCode::InvalidPayload, ".sasset source provenance contains an invalid path");
				}

				metadata.SourceDependencies.push_back(std::move(dependency));
			}

			if (!reader.IsAtEnd())
			{
				return MakeParseError(SassetErrorCode::InvalidPayload, ".sasset source provenance payload is malformed");
			}

			return std::nullopt;
		}

	} // namespace

	SassetParseResult ParseSasset(std::span<const std::byte> bytes, bool validateChunkHashes)
	{
		if (bytes.size() < HeaderSize)
		{
			return MakeParseError(SassetErrorCode::Truncated, ".sasset header is truncated");
		}

		if (!std::equal(SassetMagic.begin(), SassetMagic.end(), bytes.begin()))
		{
			return MakeParseError(SassetErrorCode::InvalidMagic, "payload does not contain the .sasset magic");
		}

		SassetParseResult result;
		SassetMetadata& metadata = result.Metadata;
		SassetTables tables;

		if (auto error = ReadHeader(bytes, metadata, tables))
		{
			return std::move(*error);
		}

		metadata.Dependencies.reserve(tables.DependencyCount);

		for (std::uint32_t index = 0; index < tables.DependencyCount; ++index)
		{
			const AssetId id{ ReadU64(bytes, static_cast<std::size_t>(tables.DependencyOffset) + index * 8u) };

			if (!id.IsValid() || id == metadata.Id)
			{
				return MakeParseError(SassetErrorCode::InvalidTable, ".sasset dependency table contains an invalid/self dependency");
			}

			metadata.Dependencies.push_back(id);
		}

		metadata.Chunks.reserve(tables.ChunkCount);
		FoundChunks found;

		for (std::uint32_t index = 0; index < tables.ChunkCount; ++index)
		{
			SassetChunkDesc chunk;

			if (auto error = ReadChunk(
					bytes, static_cast<std::size_t>(tables.ChunkOffset) + index * ChunkEntrySize, validateChunkHashes, found, chunk))
			{
				return std::move(*error);
			}

			metadata.Chunks.push_back(chunk);
		}

		if (!found.LogicalPath || !found.Payload)
		{
			return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset is missing its logical-path or asset-payload chunk");
		}

		if (auto error = ReadLogicalPath(bytes, metadata))
		{
			return std::move(*error);
		}

		const std::span<const std::byte> payloadBytes = GetSassetChunkBytes(bytes, metadata, SassetChunkType::AssetPayload);

		if (payloadBytes.empty() || (validateChunkHashes && ComputeContentHash(payloadBytes) != metadata.ContentHashValue))
		{
			return MakeParseError(SassetErrorCode::HashMismatch, ".sasset asset payload hash does not match the header content hash");
		}

		if (found.Provenance)
		{
			if (auto error = ReadSourceProvenance(bytes, metadata))
			{
				return std::move(*error);
			}
		}

		return result;
	}

	SassetLoadResult LoadSasset(AssetSystem& assets, std::span<const std::byte> bytes)
	{
		return LoadSasset(assets, bytes, true);
	}

	SassetLoadResult LoadSasset(AssetSystem& assets, std::span<const std::byte> bytes, bool validateChunkHashes)
	{
		const SassetParseResult parsed = ParseSasset(bytes, validateChunkHashes);

		if (!parsed)
		{
			SassetLoadResult result;
			result.Error = parsed.Error;
			return result;
		}

		const SassetMetadata& metadata = parsed.Metadata;

		if (!assets.GetDatabase().Bind(metadata.Id, metadata.LogicalPath))
		{
			return MakeLoadError(SassetErrorCode::PathConflict, ".sasset logical path conflicts with the runtime asset database");
		}

		BinaryReader reader(GetSassetChunkBytes(bytes, metadata, SassetChunkType::AssetPayload));
		bool published = false;

		switch (metadata.Type)
		{
		case SassetAssetType::Mesh:
			published = PublishDecoded<MeshAsset>(assets, metadata, reader, ReadMesh);
			break;
		case SassetAssetType::Texture:
			published = PublishDecoded<TextureAsset>(assets, metadata, reader, ReadTexture);
			break;
		case SassetAssetType::Sampler:
			published = PublishDecoded<SamplerAsset>(assets, metadata, reader, ReadSampler);
			break;
		case SassetAssetType::MaterialTemplate:
			published = PublishDecoded<MaterialTemplateAsset>(assets, metadata, reader, ReadMaterialTemplate);
			break;
		case SassetAssetType::Skeleton:
			published = PublishDecoded<SkeletonAsset>(assets, metadata, reader, ReadSkeleton);
			break;
		case SassetAssetType::AnimationClip:
			published = PublishDecoded<AnimationClipAsset>(assets, metadata, reader, ReadAnimationClip);
			break;
		case SassetAssetType::MaterialInstance:
		{
			const AssetHandle<MaterialInstanceAsset> handle = assets.Declare<MaterialInstanceAsset>(metadata.Id);
			assets.BeginLoading(handle);
			MaterialInstanceAsset asset{};

			if (!ReadMaterialInstance(reader, assets, asset))
			{
				assets.Fail(handle, AssetError{ AssetErrorCode::InvalidData, "invalid material-instance .sasset payload" });
				return MakeLoadError(SassetErrorCode::InvalidPayload, "invalid material-instance .sasset payload");
			}

			published = assets.Publish(handle, std::move(asset), metadata.ContentHashValue, metadata.Dependencies);
		}
		break;
		case SassetAssetType::Model:
		{
			const AssetHandle<ModelAsset> handle = assets.Declare<ModelAsset>(metadata.Id);
			assets.BeginLoading(handle);
			ModelAsset asset{};

			if (!ReadModel(reader, assets, asset))
			{
				assets.Fail(handle, AssetError{ AssetErrorCode::InvalidData, "invalid model .sasset payload" });
				return MakeLoadError(SassetErrorCode::InvalidPayload, "invalid model .sasset payload");
			}

			published = assets.Publish(handle, std::move(asset), metadata.ContentHashValue, metadata.Dependencies);
		}
		break;
		default:
			return MakeLoadError(SassetErrorCode::InvalidAssetType, "unsupported .sasset asset type");
		}

		if (!published)
		{
			return MakeLoadError(SassetErrorCode::PublishFailed, "runtime AssetSystem rejected the .sasset publish operation");
		}

		SassetLoadResult result;
		result.Id = metadata.Id;
		result.Type = metadata.Type;
		return result;
	}

	namespace
	{

		template <typename T, typename ReaderFn> bool DecodeInto(SassetDecodedAsset& decoded, BinaryReader& reader, ReaderFn&& read)
		{
			T asset{};

			if (!read(reader, asset))
			{
				return false;
			}

			decoded.Asset = std::move(asset);
			return true;
		}

		template <typename T> bool PublishValue(AssetSystem& assets, const SassetMetadata& metadata, T&& asset)
		{
			using Asset = std::decay_t<T>;
			const AssetHandle<Asset> handle = assets.Declare<Asset>(metadata.Id);
			assets.BeginLoading(handle);
			return assets.Publish(handle, std::forward<T>(asset), metadata.ContentHashValue, metadata.Dependencies);
		}

	} // namespace

	SassetDecodeResult DecodeSasset(std::span<const std::byte> bytes, bool validateChunkHashes)
	{
		SassetDecodeResult result;
		SassetParseResult parsed = ParseSasset(bytes, validateChunkHashes);

		if (!parsed)
		{
			result.Error = parsed.Error;
			return result;
		}

		result.Decoded.Metadata = std::move(parsed.Metadata);
		const SassetMetadata& metadata = result.Decoded.Metadata;

		BinaryReader reader(GetSassetChunkBytes(bytes, metadata, SassetChunkType::AssetPayload));
		bool decoded = true;

		switch (metadata.Type)
		{
		case SassetAssetType::Mesh:
			decoded = DecodeInto<MeshAsset>(result.Decoded, reader,
				[](BinaryReader& input, MeshAsset& asset)
				{
					return ReadMesh(input, asset);
				});
			break;
		case SassetAssetType::Texture:
			decoded = DecodeInto<TextureAsset>(result.Decoded, reader,
				[](BinaryReader& input, TextureAsset& asset)
				{
					return ReadTexture(input, asset);
				});
			break;
		case SassetAssetType::Sampler:
			decoded = DecodeInto<SamplerAsset>(result.Decoded, reader,
				[](BinaryReader& input, SamplerAsset& asset)
				{
					return ReadSampler(input, asset);
				});
			break;
		case SassetAssetType::MaterialTemplate:
			decoded = DecodeInto<MaterialTemplateAsset>(result.Decoded, reader,
				[](BinaryReader& input, MaterialTemplateAsset& asset)
				{
					return ReadMaterialTemplate(input, asset);
				});
			break;
		case SassetAssetType::Skeleton:
			decoded = DecodeInto<SkeletonAsset>(result.Decoded, reader,
				[](BinaryReader& input, SkeletonAsset& asset)
				{
					return ReadSkeleton(input, asset);
				});
			break;
		case SassetAssetType::AnimationClip:
			decoded = DecodeInto<AnimationClipAsset>(result.Decoded, reader,
				[](BinaryReader& input, AnimationClipAsset& asset)
				{
					return ReadAnimationClip(input, asset);
				});
			break;
		case SassetAssetType::MaterialInstance:
		case SassetAssetType::Model:
			break; // Handle-resolving payloads decode on the owner thread through LoadSasset.
		default:
			result.Error = { SassetErrorCode::InvalidAssetType, "unsupported .sasset asset type" };
			return result;
		}

		if (!decoded)
		{
			result.Decoded.Asset = std::monostate{};
			result.Error = { SassetErrorCode::InvalidPayload, "invalid .sasset payload" };
		}

		return result;
	}

	SassetLoadResult PublishSasset(AssetSystem& assets, SassetDecodedAsset decoded)
	{
		if (decoded.RequiresOwnerThreadDecode())
		{
			return MakeLoadError(SassetErrorCode::InvalidAssetType, "this .sasset type must be loaded with LoadSasset on the owner thread");
		}

		const SassetMetadata& metadata = decoded.Metadata;

		if (!assets.GetDatabase().Bind(metadata.Id, metadata.LogicalPath))
		{
			return MakeLoadError(SassetErrorCode::PathConflict, ".sasset logical path conflicts with the runtime asset database");
		}

		const bool published = std::visit(
			[&](auto&& asset) -> bool
			{
				using T = std::decay_t<decltype(asset)>;

				if constexpr (std::is_same_v<T, std::monostate>)
				{
					return false;
				}
				else
				{
					return PublishValue(assets, metadata, std::move(asset));
				}

			},
			decoded.Asset);

		if (!published)
		{
			return MakeLoadError(SassetErrorCode::PublishFailed, "runtime AssetSystem rejected the .sasset publish operation");
		}

		SassetLoadResult result;
		result.Id = metadata.Id;
		result.Type = metadata.Type;
		return result;
	}

} // namespace Swim::Assets
