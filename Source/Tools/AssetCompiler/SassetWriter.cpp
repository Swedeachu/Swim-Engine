#include "Tools/AssetCompiler/SassetWriter.h"

#include "Engine/Assets/AssetDatabase.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace Swim::AssetCompiler
{
	namespace
	{
		constexpr std::array<std::byte, 8> SassetMagic
		{
			std::byte{ 'S' }, std::byte{ 'A' }, std::byte{ 'S' }, std::byte{ 'S' },
			std::byte{ 'E' }, std::byte{ 'T' }, std::byte{ 0x0D }, std::byte{ 0x0A }
		};

		class BinaryWriter
		{
		public:
			void U8(std::uint8_t value)
			{
				bytes.push_back(static_cast<std::byte>(value));
			}

			void U16(std::uint16_t value)
			{
				U8(static_cast<std::uint8_t>(value));
				U8(static_cast<std::uint8_t>(value >> 8));
			}

			void U32(std::uint32_t value)
			{
				for (std::size_t index = 0; index < 4; ++index)
				{
					U8(static_cast<std::uint8_t>(value >> (index * 8)));
				}
			}

			void I32(std::int32_t value)
			{
				U32(static_cast<std::uint32_t>(value));
			}

			void U64(std::uint64_t value)
			{
				U32(static_cast<std::uint32_t>(value));
				U32(static_cast<std::uint32_t>(value >> 32));
			}

			void F32(float value)
			{
				U32(std::bit_cast<std::uint32_t>(value));
			}

			void String(std::string_view value)
			{
				if (value.size() > std::numeric_limits<std::uint32_t>::max())
				{
					throw std::overflow_error("string exceeds .sasset v1 32-bit length field");
				}
				U32(static_cast<std::uint32_t>(value.size()));
				Append(std::as_bytes(std::span(value.data(), value.size())));
			}

			void Hash(const Swim::Assets::ContentHash& hash)
			{
				for (const std::uint8_t byte : hash.Bytes)
				{
					U8(byte);
				}
			}

			void ByteVector(std::span<const std::byte> value)
			{
				U64(value.size());
				Append(value);
			}

			void Append(std::span<const std::byte> value)
			{
				bytes.insert(bytes.end(), value.begin(), value.end());
			}

			void Align(std::size_t alignment)
			{
				const std::size_t remainder = bytes.size() % alignment;
				if (remainder == 0)
				{
					return;
				}
				bytes.resize(bytes.size() + (alignment - remainder));
			}

			std::size_t Size() const { return bytes.size(); }
			const std::vector<std::byte>& Bytes() const { return bytes; }
			std::vector<std::byte>& Bytes() { return bytes; }

		private:
			std::vector<std::byte> bytes;
		};

		void WriteBounds(BinaryWriter& writer, const Swim::Assets::AssetBounds& bounds)
		{
			for (const float value : bounds.Min)
			{
				writer.F32(value);
			}
			for (const float value : bounds.Max)
			{
				writer.F32(value);
			}
		}

		void WriteTransform(BinaryWriter& writer, const Swim::Assets::AssetTransform& transform)
		{
			for (const float value : transform.Translation)
			{
				writer.F32(value);
			}
			for (const float value : transform.Rotation)
			{
				writer.F32(value);
			}
			for (const float value : transform.Scale)
			{
				writer.F32(value);
			}
		}

		void WriteU32At(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
		{
			for (std::size_t index = 0; index < 4; ++index)
			{
				bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFu);
			}
		}

		void WriteU64At(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value)
		{
			WriteU32At(bytes, offset, static_cast<std::uint32_t>(value));
			WriteU32At(bytes, offset + 4, static_cast<std::uint32_t>(value >> 32));
		}

		void WriteHashAt(std::vector<std::byte>& bytes, std::size_t offset, const Swim::Assets::ContentHash& hash)
		{
			for (std::size_t index = 0; index < hash.Bytes.size(); ++index)
			{
				bytes[offset + index] = static_cast<std::byte>(hash.Bytes[index]);
			}
		}

		std::vector<std::byte> SerializeProvenance(std::span<const Swim::Assets::SassetSourceDependency> dependencies)
		{
			BinaryWriter writer;
			writer.U32(Swim::Assets::SassetPayloadVersion);
			if (dependencies.size() > std::numeric_limits<std::uint32_t>::max())
			{
				throw std::overflow_error("source dependency count exceeds .sasset v1 limits");
			}
			writer.U32(static_cast<std::uint32_t>(dependencies.size()));
			for (const Swim::Assets::SassetSourceDependency& dependency : dependencies)
			{
				writer.String(Swim::Assets::NormalizeAssetPath(dependency.LogicalPath));
				writer.Hash(dependency.Hash);
			}
			return std::move(writer.Bytes());
		}

		struct PendingChunk
		{
			Swim::Assets::SassetChunkType Type = Swim::Assets::SassetChunkType::AssetPayload;
			std::uint32_t Alignment = 1;
			std::vector<std::byte> Bytes;
		};
	}

	Swim::Assets::ContentHash ComputeSourceGraphHash(std::span<const Swim::Assets::SassetSourceDependency> dependencies)
	{
		std::vector<Swim::Assets::SassetSourceDependency> ordered(dependencies.begin(), dependencies.end());
		std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
		{
			return left.LogicalPath < right.LogicalPath;
		});

		BinaryWriter writer;
		writer.U32(static_cast<std::uint32_t>(ordered.size()));
		for (const auto& dependency : ordered)
		{
			writer.String(Swim::Assets::NormalizeAssetPath(dependency.LogicalPath));
			writer.Hash(dependency.Hash);
		}
		return Swim::Assets::ComputeContentHash(writer.Bytes());
	}

	SassetBuildResult BuildSasset(const SassetBuildInput& input)
	{
		SassetBuildResult result;
		try
		{
			if (!input.Id.IsValid())
			{
				result.Error = { Swim::Assets::SassetErrorCode::InvalidAssetId, "cannot build .sasset with an invalid AssetId" };
				return result;
			}
			if (input.Type == Swim::Assets::SassetAssetType::Unknown || input.Payload.empty())
			{
				result.Error = { Swim::Assets::SassetErrorCode::InvalidPayload, "cannot build .sasset without a known type and payload" };
				return result;
			}

			const std::string logicalPath = Swim::Assets::NormalizeAssetPath(input.LogicalPath);
			std::vector<PendingChunk> chunks;
			PendingChunk logicalPathChunk;
			logicalPathChunk.Type = Swim::Assets::SassetChunkType::LogicalPath;
			logicalPathChunk.Alignment = 1;
			logicalPathChunk.Bytes.assign(
				reinterpret_cast<const std::byte*>(logicalPath.data()),
				reinterpret_cast<const std::byte*>(logicalPath.data() + logicalPath.size()));
			chunks.push_back(std::move(logicalPathChunk));

			if (!input.SourceDependencies.empty())
			{
				PendingChunk provenanceChunk;
				provenanceChunk.Type = Swim::Assets::SassetChunkType::SourceProvenance;
				provenanceChunk.Alignment = 8;
				provenanceChunk.Bytes = SerializeProvenance(input.SourceDependencies);
				chunks.push_back(std::move(provenanceChunk));
			}

			PendingChunk payloadChunk;
			payloadChunk.Type = Swim::Assets::SassetChunkType::AssetPayload;
			payloadChunk.Alignment = 16;
			payloadChunk.Bytes = input.Payload;
			chunks.push_back(std::move(payloadChunk));

			if (input.Dependencies.size() > std::numeric_limits<std::uint32_t>::max() || chunks.size() > std::numeric_limits<std::uint32_t>::max())
			{
				result.Error = { Swim::Assets::SassetErrorCode::InvalidTable, ".sasset dependency/chunk count exceeds v1 limits" };
				return result;
			}

			BinaryWriter writer;
			writer.Bytes().resize(Swim::Assets::SassetHeaderSize);
			const std::uint64_t dependencyTableOffset = writer.Size();
			for (const Swim::Assets::AssetId dependency : input.Dependencies)
			{
				if (!dependency.IsValid() || dependency == input.Id)
				{
					result.Error = { Swim::Assets::SassetErrorCode::InvalidTable, ".sasset dependency list contains an invalid/self dependency" };
					return result;
				}
				writer.U64(dependency.Value);
			}
			writer.Align(8);
			const std::uint64_t chunkTableOffset = writer.Size();
			writer.Bytes().resize(writer.Size() + chunks.size() * Swim::Assets::SassetChunkEntrySize);

			struct WrittenChunk
			{
				std::uint64_t Offset = 0;
				Swim::Assets::ContentHash Hash{};
			};
			std::vector<WrittenChunk> writtenChunks(chunks.size());
			for (std::size_t index = 0; index < chunks.size(); ++index)
			{
				writer.Align(chunks[index].Alignment);
				writtenChunks[index].Offset = writer.Size();
				writtenChunks[index].Hash = Swim::Assets::ComputeContentHash(chunks[index].Bytes);
				writer.Append(chunks[index].Bytes);
			}

			std::vector<std::byte>& bytes = writer.Bytes();
			std::copy(SassetMagic.begin(), SassetMagic.end(), bytes.begin());
			WriteU32At(bytes, 8, Swim::Assets::SassetSchemaVersion);
			WriteU32At(bytes, 12, static_cast<std::uint32_t>(Swim::Assets::SassetHeaderSize));
			WriteU32At(bytes, 16, static_cast<std::uint32_t>(input.Type));
			WriteU32At(bytes, 20, 0);
			WriteU64At(bytes, 24, input.Id.Value);

			const Swim::Assets::ContentHash contentHash = Swim::Assets::ComputeContentHash(input.Payload);
			WriteHashAt(bytes, 32, contentHash);
			WriteHashAt(bytes, 64, input.CompilerProfileHash);
			WriteHashAt(bytes, 96, input.SourceHash);
			WriteU64At(bytes, 128, dependencyTableOffset);
			WriteU32At(bytes, 136, static_cast<std::uint32_t>(input.Dependencies.size()));
			WriteU32At(bytes, 140, static_cast<std::uint32_t>(chunks.size()));
			WriteU64At(bytes, 144, chunkTableOffset);
			WriteU64At(bytes, 152, bytes.size());

			for (std::size_t index = 0; index < chunks.size(); ++index)
			{
				const std::size_t offset = static_cast<std::size_t>(chunkTableOffset) + index * Swim::Assets::SassetChunkEntrySize;
				WriteU32At(bytes, offset + 0, static_cast<std::uint32_t>(chunks[index].Type));
				WriteU32At(bytes, offset + 4, static_cast<std::uint32_t>(Swim::Assets::SassetCompression::None));
				WriteU64At(bytes, offset + 8, writtenChunks[index].Offset);
				WriteU64At(bytes, offset + 16, chunks[index].Bytes.size());
				WriteU64At(bytes, offset + 24, chunks[index].Bytes.size());
				WriteU32At(bytes, offset + 32, chunks[index].Alignment);
				WriteU32At(bytes, offset + 36, 0);
				WriteHashAt(bytes, offset + 40, writtenChunks[index].Hash);
			}

			result.Bytes = std::move(bytes);
		}
		catch (const std::exception& error)
		{
			result.Bytes.clear();
			result.Error = { Swim::Assets::SassetErrorCode::InvalidPayload, error.what() };
		}
		return result;
	}

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::MeshAsset& asset)
	{
		BinaryWriter writer;
		writer.U32(Swim::Assets::SassetPayloadVersion);
		writer.U8(static_cast<std::uint8_t>(asset.IndexFormat));
		WriteBounds(writer, asset.Bounds);

		writer.U32(static_cast<std::uint32_t>(asset.VertexStreams.size()));
		for (const auto& stream : asset.VertexStreams)
		{
			writer.U32(stream.StrideBytes);
			writer.U64(stream.DataOffsetBytes);
			writer.U64(stream.DataSizeBytes);
		}
		writer.U32(static_cast<std::uint32_t>(asset.VertexAttributes.size()));
		for (const auto& attribute : asset.VertexAttributes)
		{
			writer.U8(static_cast<std::uint8_t>(attribute.Semantic));
			writer.U8(static_cast<std::uint8_t>(attribute.Format));
			writer.U16(attribute.StreamIndex);
			writer.U32(attribute.OffsetBytes);
		}
		writer.U32(static_cast<std::uint32_t>(asset.Primitives.size()));
		for (const auto& primitive : asset.Primitives)
		{
			writer.U32(primitive.FirstIndex);
			writer.U32(primitive.IndexCount);
			writer.I32(primitive.VertexOffset);
			writer.U32(primitive.MaterialSlot);
			WriteBounds(writer, primitive.Bounds);
		}
		writer.U32(static_cast<std::uint32_t>(asset.Lods.size()));
		for (const auto& lod : asset.Lods)
		{
			writer.U32(lod.FirstPrimitive);
			writer.U32(lod.PrimitiveCount);
			writer.F32(lod.ScreenCoverage);
		}
		writer.U32(static_cast<std::uint32_t>(asset.Meshlets.size()));
		for (const auto& meshlet : asset.Meshlets)
		{
			writer.U32(meshlet.VertexOffset);
			writer.U32(meshlet.VertexCount);
			writer.U32(meshlet.TriangleOffset);
			writer.U32(meshlet.TriangleCount);
		}
		writer.ByteVector(asset.VertexBytes);
		writer.ByteVector(asset.IndexBytes);
		writer.ByteVector(asset.MeshletVertexBytes);
		writer.ByteVector(asset.MeshletTriangleBytes);
		return std::move(writer.Bytes());
	}

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::TextureAsset& asset)
	{
		BinaryWriter writer;
		writer.U32(Swim::Assets::SassetPayloadVersion);
		writer.U8(static_cast<std::uint8_t>(asset.Dimension));
		writer.U8(static_cast<std::uint8_t>(asset.ColorSpace));
		writer.U8(static_cast<std::uint8_t>(asset.Semantic));
		writer.U32(asset.Width);
		writer.U32(asset.Height);
		writer.U32(asset.Depth);
		writer.U32(asset.ArrayLayers);
		writer.U32(static_cast<std::uint32_t>(asset.Payloads.size()));
		for (const auto& payload : asset.Payloads)
		{
			writer.U8(static_cast<std::uint8_t>(payload.Container));
			writer.U8(static_cast<std::uint8_t>(payload.Format));
			writer.U8(static_cast<std::uint8_t>(payload.Supercompression));
			writer.U32(payload.ContainerFormatCode);
			writer.U32(static_cast<std::uint32_t>(payload.Mips.size()));
			for (const auto& mip : payload.Mips)
			{
				writer.U32(mip.Width);
				writer.U32(mip.Height);
				writer.U32(mip.Depth);
				writer.U64(mip.OffsetBytes);
				writer.U64(mip.SizeBytes);
				writer.U64(mip.UncompressedSizeBytes);
			}
			writer.ByteVector(payload.Bytes);
		}
		return std::move(writer.Bytes());
	}

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::SamplerAsset& asset)
	{
		BinaryWriter writer;
		writer.U32(Swim::Assets::SassetPayloadVersion);
		writer.U8(static_cast<std::uint8_t>(asset.MinFilter));
		writer.U8(static_cast<std::uint8_t>(asset.MagFilter));
		writer.U8(static_cast<std::uint8_t>(asset.MipFilter));
		writer.U8(static_cast<std::uint8_t>(asset.AddressU));
		writer.U8(static_cast<std::uint8_t>(asset.AddressV));
		writer.U8(static_cast<std::uint8_t>(asset.AddressW));
		writer.F32(asset.MaxAnisotropy);
		return std::move(writer.Bytes());
	}

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::MaterialTemplateAsset& asset)
	{
		BinaryWriter writer;
		writer.U32(Swim::Assets::SassetPayloadVersion);
		writer.String(asset.ShaderFamily);
		writer.U64(asset.FeatureMask);
		writer.U32(static_cast<std::uint32_t>(asset.Parameters.size()));
		for (const auto& parameter : asset.Parameters)
		{
			writer.String(parameter.Name);
			writer.U8(static_cast<std::uint8_t>(parameter.Type));
			for (const float value : parameter.DefaultValue)
			{
				writer.F32(value);
			}
		}
		return std::move(writer.Bytes());
	}

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::MaterialInstanceAsset& asset)
	{
		BinaryWriter writer;
		writer.U32(Swim::Assets::SassetPayloadVersion);
		writer.U64(asset.Template.GetId().Value);
		writer.U32(static_cast<std::uint32_t>(asset.Parameters.size()));
		for (const auto& parameter : asset.Parameters)
		{
			writer.String(parameter.Name);
			for (const float value : parameter.Value)
			{
				writer.F32(value);
			}
		}
		writer.U32(static_cast<std::uint32_t>(asset.Textures.size()));
		for (const auto& texture : asset.Textures)
		{
			writer.String(texture.Name);
			writer.U64(texture.Texture.GetId().Value);
			writer.U64(texture.Sampler.GetId().Value);
		}
		return std::move(writer.Bytes());
	}

	std::vector<std::byte> SerializeAssetPayload(const Swim::Assets::ModelAsset& asset)
	{
		BinaryWriter writer;
		writer.U32(Swim::Assets::SassetPayloadVersion);
		writer.U32(static_cast<std::uint32_t>(asset.Nodes.size()));
		for (const auto& node : asset.Nodes)
		{
			writer.String(node.Name);
			writer.U32(node.Parent);
			WriteTransform(writer, node.LocalTransform);
			writer.U64(node.Mesh.GetId().Value);
			writer.U32(static_cast<std::uint32_t>(node.Materials.size()));
			for (const auto material : node.Materials)
			{
				writer.U64(material.GetId().Value);
			}
		}
		writer.U32(static_cast<std::uint32_t>(asset.Roots.size()));
		for (const std::uint32_t root : asset.Roots)
		{
			writer.U32(root);
		}
		return std::move(writer.Bytes());
	}

}
