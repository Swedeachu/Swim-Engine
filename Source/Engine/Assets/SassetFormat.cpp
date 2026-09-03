#include "Engine/Assets/SassetFormat.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/TextureAsset.h"

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Swim::Assets
{
	namespace
	{
		constexpr std::array<std::byte, 8> SassetMagic
		{
			std::byte{ 'S' }, std::byte{ 'A' }, std::byte{ 'S' }, std::byte{ 'S' },
			std::byte{ 'E' }, std::byte{ 'T' }, std::byte{ 0x0D }, std::byte{ 0x0A }
		};
		constexpr std::size_t HeaderSize = SassetHeaderSize;
		constexpr std::size_t ChunkEntrySize = SassetChunkEntrySize;
		constexpr std::size_t ChunkHashOffset = 40;
		constexpr std::size_t MaxCollectionCount = 1u << 24;

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

		std::uint16_t ReadU16(std::span<const std::byte> bytes, std::size_t offset)
		{
			return
				static_cast<std::uint16_t>(bytes[offset + 0]) |
				(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
		}

		std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset)
		{
			return
				static_cast<std::uint32_t>(bytes[offset + 0]) |
				(static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
				(static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
				(static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
		}

		std::uint64_t ReadU64(std::span<const std::byte> bytes, std::size_t offset)
		{
			return
				static_cast<std::uint64_t>(ReadU32(bytes, offset)) |
				(static_cast<std::uint64_t>(ReadU32(bytes, offset + 4)) << 32);
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
				return true;
			default:
				return false;
			}
		}

		class BinaryReader
		{
		public:
			explicit BinaryReader(std::span<const std::byte> bytes)
				: bytes(bytes)
			{
			}

			bool IsValid() const { return valid; }
			bool IsAtEnd() const { return valid && offset == bytes.size(); }

			std::uint8_t U8()
			{
				if (!Require(1))
				{
					return 0;
				}
				return static_cast<std::uint8_t>(bytes[offset++]);
			}

			std::uint16_t U16()
			{
				if (!Require(2))
				{
					return 0;
				}
				const std::uint16_t value = ReadU16(bytes, offset);
				offset += 2;
				return value;
			}

			std::uint32_t U32()
			{
				if (!Require(4))
				{
					return 0;
				}
				const std::uint32_t value = ReadU32(bytes, offset);
				offset += 4;
				return value;
			}

			std::int32_t I32()
			{
				return static_cast<std::int32_t>(U32());
			}

			std::uint64_t U64()
			{
				if (!Require(8))
				{
					return 0;
				}
				const std::uint64_t value = ReadU64(bytes, offset);
				offset += 8;
				return value;
			}

			float F32()
			{
				return std::bit_cast<float>(U32());
			}

			std::string String()
			{
				const std::uint32_t size = U32();
				if (!valid || !Require(size))
				{
					return {};
				}
				std::string value(size, '\0');
				if (size > 0)
				{
					std::memcpy(value.data(), bytes.data() + offset, size);
				}
				offset += size;
				return value;
			}

			std::vector<std::byte> ByteVector()
			{
				const std::uint64_t size64 = U64();
				if (!valid || size64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
				{
					valid = false;
					return {};
				}
				const std::size_t size = static_cast<std::size_t>(size64);
				if (!Require(size))
				{
					return {};
				}
				std::vector<std::byte> value(size);
				if (size > 0)
				{
					std::memcpy(value.data(), bytes.data() + offset, size);
				}
				offset += size;
				return value;
			}

			bool Count(std::uint32_t& count)
			{
				count = U32();
				if (!valid || count > MaxCollectionCount)
				{
					valid = false;
					return false;
				}
				return true;
			}

		private:
			bool Require(std::size_t count)
			{
				if (!valid || offset > bytes.size() || count > bytes.size() - offset)
				{
					valid = false;
					return false;
				}
				return true;
			}

			std::span<const std::byte> bytes;
			std::size_t offset = 0;
			bool valid = true;
		};

		bool ReadBounds(BinaryReader& reader, AssetBounds& bounds)
		{
			for (float& value : bounds.Min)
			{
				value = reader.F32();
			}
			for (float& value : bounds.Max)
			{
				value = reader.F32();
			}
			return reader.IsValid();
		}

		bool ReadTransform(BinaryReader& reader, AssetTransform& transform)
		{
			for (float& value : transform.Translation)
			{
				value = reader.F32();
			}
			for (float& value : transform.Rotation)
			{
				value = reader.F32();
			}
			for (float& value : transform.Scale)
			{
				value = reader.F32();
			}
			return reader.IsValid();
		}

		template<typename T>
		AssetHandle<T> DeclareReference(AssetSystem& assets, AssetId id)
		{
			if (!id.IsValid())
			{
				return {};
			}
			return assets.Declare<T>(id);
		}

		bool ReadMesh(BinaryReader& reader, MeshAsset& asset)
		{
			if (reader.U32() != SassetPayloadVersion)
			{
				return false;
			}
			asset.IndexFormat = static_cast<IndexElementFormat>(reader.U8());
			if (!ReadBounds(reader, asset.Bounds))
			{
				return false;
			}

			std::uint32_t count = 0;
			if (!reader.Count(count))
			{
				return false;
			}
			asset.VertexStreams.resize(count);
			for (VertexStreamDesc& stream : asset.VertexStreams)
			{
				stream.StrideBytes = reader.U32();
				stream.DataOffsetBytes = reader.U64();
				stream.DataSizeBytes = reader.U64();
			}

			if (!reader.Count(count))
			{
				return false;
			}
			asset.VertexAttributes.resize(count);
			for (VertexAttributeDesc& attribute : asset.VertexAttributes)
			{
				attribute.Semantic = static_cast<VertexSemantic>(reader.U8());
				attribute.Format = static_cast<VertexElementFormat>(reader.U8());
				attribute.StreamIndex = reader.U16();
				attribute.OffsetBytes = reader.U32();
			}

			if (!reader.Count(count))
			{
				return false;
			}
			asset.Primitives.resize(count);
			for (MeshPrimitive& primitive : asset.Primitives)
			{
				primitive.FirstIndex = reader.U32();
				primitive.IndexCount = reader.U32();
				primitive.VertexOffset = reader.I32();
				primitive.MaterialSlot = reader.U32();
				ReadBounds(reader, primitive.Bounds);
			}

			if (!reader.Count(count))
			{
				return false;
			}
			asset.Lods.resize(count);
			for (MeshLod& lod : asset.Lods)
			{
				lod.FirstPrimitive = reader.U32();
				lod.PrimitiveCount = reader.U32();
				lod.ScreenCoverage = reader.F32();
			}

			if (!reader.Count(count))
			{
				return false;
			}
			asset.Meshlets.resize(count);
			for (MeshletDesc& meshlet : asset.Meshlets)
			{
				meshlet.VertexOffset = reader.U32();
				meshlet.VertexCount = reader.U32();
				meshlet.TriangleOffset = reader.U32();
				meshlet.TriangleCount = reader.U32();
			}

			asset.VertexBytes = reader.ByteVector();
			asset.IndexBytes = reader.ByteVector();
			asset.MeshletVertexBytes = reader.ByteVector();
			asset.MeshletTriangleBytes = reader.ByteVector();
			return reader.IsAtEnd();
		}

		bool ReadTexture(BinaryReader& reader, TextureAsset& asset)
		{
			if (reader.U32() != SassetPayloadVersion)
			{
				return false;
			}
			asset.Dimension = static_cast<TextureDimension>(reader.U8());
			asset.ColorSpace = static_cast<TextureColorSpace>(reader.U8());
			asset.Semantic = static_cast<TextureSemantic>(reader.U8());
			asset.Width = reader.U32();
			asset.Height = reader.U32();
			asset.Depth = reader.U32();
			asset.ArrayLayers = reader.U32();

			std::uint32_t count = 0;
			if (!reader.Count(count))
			{
				return false;
			}
			asset.Payloads.resize(count);
			for (TexturePayloadVariant& payload : asset.Payloads)
			{
				payload.Container = static_cast<TextureContainerFormat>(reader.U8());
				payload.Format = static_cast<TexturePayloadFormat>(reader.U8());
				payload.Supercompression = static_cast<TextureSupercompression>(reader.U8());
				payload.ContainerFormatCode = reader.U32();

				std::uint32_t mipCount = 0;
				if (!reader.Count(mipCount))
				{
					return false;
				}
				payload.Mips.resize(mipCount);
				for (TextureMipDesc& mip : payload.Mips)
				{
					mip.Width = reader.U32();
					mip.Height = reader.U32();
					mip.Depth = reader.U32();
					mip.OffsetBytes = reader.U64();
					mip.SizeBytes = reader.U64();
					mip.UncompressedSizeBytes = reader.U64();
				}
				payload.Bytes = reader.ByteVector();
			}
			return reader.IsAtEnd();
		}

		bool ReadSampler(BinaryReader& reader, SamplerAsset& asset)
		{
			if (reader.U32() != SassetPayloadVersion)
			{
				return false;
			}
			asset.MinFilter = static_cast<SamplerFilter>(reader.U8());
			asset.MagFilter = static_cast<SamplerFilter>(reader.U8());
			asset.MipFilter = static_cast<SamplerFilter>(reader.U8());
			asset.AddressU = static_cast<SamplerAddressMode>(reader.U8());
			asset.AddressV = static_cast<SamplerAddressMode>(reader.U8());
			asset.AddressW = static_cast<SamplerAddressMode>(reader.U8());
			asset.MaxAnisotropy = reader.F32();
			return reader.IsAtEnd();
		}

		bool ReadMaterialTemplate(BinaryReader& reader, MaterialTemplateAsset& asset)
		{
			if (reader.U32() != SassetPayloadVersion)
			{
				return false;
			}
			asset.ShaderFamily = reader.String();
			asset.FeatureMask = reader.U64();
			std::uint32_t count = 0;
			if (!reader.Count(count))
			{
				return false;
			}
			asset.Parameters.resize(count);
			for (MaterialParameterDesc& parameter : asset.Parameters)
			{
				parameter.Name = reader.String();
				parameter.Type = static_cast<MaterialParameterType>(reader.U8());
				for (float& value : parameter.DefaultValue)
				{
					value = reader.F32();
				}
			}
			return reader.IsAtEnd();
		}

		bool ReadMaterialInstance(BinaryReader& reader, AssetSystem& assets, MaterialInstanceAsset& asset)
		{
			if (reader.U32() != SassetPayloadVersion)
			{
				return false;
			}
			asset.Template = DeclareReference<MaterialTemplateAsset>(assets, AssetId{ reader.U64() });

			std::uint32_t count = 0;
			if (!reader.Count(count))
			{
				return false;
			}
			asset.Parameters.resize(count);
			for (MaterialParameterValue& parameter : asset.Parameters)
			{
				parameter.Name = reader.String();
				for (float& value : parameter.Value)
				{
					value = reader.F32();
				}
			}

			if (!reader.Count(count))
			{
				return false;
			}
			asset.Textures.resize(count);
			for (MaterialTextureBinding& texture : asset.Textures)
			{
				texture.Name = reader.String();
				texture.Texture = DeclareReference<TextureAsset>(assets, AssetId{ reader.U64() });
				texture.Sampler = DeclareReference<SamplerAsset>(assets, AssetId{ reader.U64() });
			}
			return reader.IsAtEnd();
		}

		bool ReadModel(BinaryReader& reader, AssetSystem& assets, ModelAsset& asset)
		{
			if (reader.U32() != SassetPayloadVersion)
			{
				return false;
			}
			std::uint32_t count = 0;
			if (!reader.Count(count))
			{
				return false;
			}
			asset.Nodes.resize(count);
			for (ModelNode& node : asset.Nodes)
			{
				node.Name = reader.String();
				node.Parent = reader.U32();
				ReadTransform(reader, node.LocalTransform);
				node.Mesh = DeclareReference<MeshAsset>(assets, AssetId{ reader.U64() });

				std::uint32_t materialCount = 0;
				if (!reader.Count(materialCount))
				{
					return false;
				}
				node.Materials.resize(materialCount);
				for (AssetHandle<MaterialInstanceAsset>& material : node.Materials)
				{
					material = DeclareReference<MaterialInstanceAsset>(assets, AssetId{ reader.U64() });
				}
			}

			if (!reader.Count(count))
			{
				return false;
			}
			asset.Roots.resize(count);
			for (std::uint32_t& root : asset.Roots)
			{
				root = reader.U32();
			}
			return reader.IsAtEnd();
		}

		template<typename T, typename ReaderFn>
		bool PublishDecoded(
			AssetSystem& assets,
			const SassetMetadata& metadata,
			BinaryReader& reader,
			ReaderFn&& read)
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
	}

	std::span<const std::byte> GetSassetChunkBytes(
		std::span<const std::byte> bytes,
		const SassetMetadata& metadata,
		SassetChunkType type)
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
		metadata.SchemaVersion = ReadU32(bytes, 8);
		const std::uint32_t headerSize = ReadU32(bytes, 12);
		metadata.Type = static_cast<SassetAssetType>(ReadU32(bytes, 16));
		metadata.Id = AssetId{ ReadU64(bytes, 24) };
		metadata.ContentHashValue = ReadHash(bytes, 32);
		metadata.CompilerProfileHash = ReadHash(bytes, 64);
		metadata.SourceHash = ReadHash(bytes, 96);
		const std::uint64_t dependencyTableOffset = ReadU64(bytes, 128);
		const std::uint32_t dependencyCount = ReadU32(bytes, 136);
		const std::uint32_t chunkCount = ReadU32(bytes, 140);
		const std::uint64_t chunkTableOffset = ReadU64(bytes, 144);
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
		if (dependencyCount > MaxCollectionCount || chunkCount > MaxCollectionCount)
		{
			return MakeParseError(SassetErrorCode::InvalidTable, ".sasset table count exceeds runtime limits");
		}
		if (!RangeFits(dependencyTableOffset, static_cast<std::uint64_t>(dependencyCount) * 8u, bytes.size()))
		{
			return MakeParseError(SassetErrorCode::InvalidTable, ".sasset dependency table is out of range");
		}
		if (!RangeFits(chunkTableOffset, static_cast<std::uint64_t>(chunkCount) * ChunkEntrySize, bytes.size()))
		{
			return MakeParseError(SassetErrorCode::InvalidTable, ".sasset chunk table is out of range");
		}

		metadata.Dependencies.reserve(dependencyCount);
		for (std::uint32_t index = 0; index < dependencyCount; ++index)
		{
			const AssetId id{ ReadU64(bytes, static_cast<std::size_t>(dependencyTableOffset) + index * 8u) };
			if (!id.IsValid() || id == metadata.Id)
			{
				return MakeParseError(SassetErrorCode::InvalidTable, ".sasset dependency table contains an invalid/self dependency");
			}
			metadata.Dependencies.push_back(id);
		}

		metadata.Chunks.reserve(chunkCount);
		bool foundLogicalPath = false;
		bool foundPayload = false;
		bool foundProvenance = false;
		for (std::uint32_t index = 0; index < chunkCount; ++index)
		{
			const std::size_t offset = static_cast<std::size_t>(chunkTableOffset) + index * ChunkEntrySize;
			SassetChunkDesc chunk;
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
				return MakeParseError(SassetErrorCode::UnsupportedCompression, ".sasset v1 runtime currently accepts uncompressed chunks only");
			}
			if (chunk.UncompressedSizeBytes != chunk.SizeBytes)
			{
				return MakeParseError(SassetErrorCode::InvalidChunk, "uncompressed .sasset chunk has mismatched size fields");
			}
			if (validateChunkHashes)
			{
				const auto chunkBytes = bytes.subspan(static_cast<std::size_t>(chunk.OffsetBytes), static_cast<std::size_t>(chunk.SizeBytes));
				if (ComputeContentHash(chunkBytes) != chunk.Hash)
				{
					return MakeParseError(SassetErrorCode::HashMismatch, ".sasset chunk content hash mismatch");
				}
			}

			switch (chunk.Type)
			{
			case SassetChunkType::LogicalPath:
				if (foundLogicalPath)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains duplicate logical-path chunks");
				}
				foundLogicalPath = true;
				break;
			case SassetChunkType::SourceProvenance:
				if (foundProvenance)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains duplicate source-provenance chunks");
				}
				foundProvenance = true;
				break;
			case SassetChunkType::AssetPayload:
				if (foundPayload)
				{
					return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains duplicate asset-payload chunks");
				}
				foundPayload = true;
				break;
			default:
				return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset contains an unknown required chunk type");
			}
			metadata.Chunks.push_back(chunk);
		}

		if (!foundLogicalPath || !foundPayload)
		{
			return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset is missing its logical-path or asset-payload chunk");
		}

		const std::span<const std::byte> logicalPathBytes = GetSassetChunkBytes(bytes, metadata, SassetChunkType::LogicalPath);
		if (logicalPathBytes.empty())
		{
			return MakeParseError(SassetErrorCode::InvalidChunk, ".sasset logical path cannot be empty");
		}
		metadata.LogicalPath.assign(
			reinterpret_cast<const char*>(logicalPathBytes.data()),
			logicalPathBytes.size());
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

		const std::span<const std::byte> payloadBytes = GetSassetChunkBytes(bytes, metadata, SassetChunkType::AssetPayload);
		if (payloadBytes.empty() || ComputeContentHash(payloadBytes) != metadata.ContentHashValue)
		{
			return MakeParseError(SassetErrorCode::HashMismatch, ".sasset asset payload hash does not match the header content hash");
		}

		if (foundProvenance)
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
		}

		return result;
	}

	SassetLoadResult LoadSasset(AssetSystem& assets, std::span<const std::byte> bytes)
	{
		const SassetParseResult parsed = ParseSasset(bytes, true);
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
			published = PublishDecoded<MeshAsset>(assets, metadata, reader,
				[](BinaryReader& input, MeshAsset& asset) { return ReadMesh(input, asset); });
			break;
		case SassetAssetType::Texture:
			published = PublishDecoded<TextureAsset>(assets, metadata, reader,
				[](BinaryReader& input, TextureAsset& asset) { return ReadTexture(input, asset); });
			break;
		case SassetAssetType::Sampler:
			published = PublishDecoded<SamplerAsset>(assets, metadata, reader,
				[](BinaryReader& input, SamplerAsset& asset) { return ReadSampler(input, asset); });
			break;
		case SassetAssetType::MaterialTemplate:
			published = PublishDecoded<MaterialTemplateAsset>(assets, metadata, reader,
				[](BinaryReader& input, MaterialTemplateAsset& asset) { return ReadMaterialTemplate(input, asset); });
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

}
