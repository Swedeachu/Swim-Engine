#pragma once
#include "Engine/Assets/SassetFormat.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SkeletonAsset.h"
#include "Engine/Assets/TextureAsset.h"

#include <bit>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// The .sasset payload reader and the per-type payload decoders (SassetPayloadReaders.cpp).
namespace Swim::Assets::SassetDetail
{

	constexpr std::size_t MaxCollectionCount = 1u << 24;

	inline std::uint16_t ReadU16(std::span<const std::byte> bytes, std::size_t offset)
	{
		return static_cast<std::uint16_t>(bytes[offset + 0]) | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
	}

	inline std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset)
	{
		return static_cast<std::uint32_t>(bytes[offset + 0]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
			(static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	inline std::uint64_t ReadU64(std::span<const std::byte> bytes, std::size_t offset)
	{
		return static_cast<std::uint64_t>(ReadU32(bytes, offset)) | (static_cast<std::uint64_t>(ReadU32(bytes, offset + 4)) << 32);
	}

	class BinaryReader
	{

	  public:
		explicit BinaryReader(std::span<const std::byte> bytes) : bytes(bytes) {}

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

		std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

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

		float F32() { return std::bit_cast<float>(U32()); }

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

	bool ReadMesh(BinaryReader& reader, MeshAsset& asset);
	bool ReadSkeleton(BinaryReader& reader, SkeletonAsset& asset);
	bool ReadAnimationClip(BinaryReader& reader, AnimationClipAsset& asset);
	bool ReadTexture(BinaryReader& reader, TextureAsset& asset);
	bool ReadSampler(BinaryReader& reader, SamplerAsset& asset);
	bool ReadMaterialTemplate(BinaryReader& reader, MaterialTemplateAsset& asset);
	bool ReadMaterialInstance(BinaryReader& reader, AssetSystem& assets, MaterialInstanceAsset& asset);
	bool ReadModel(BinaryReader& reader, AssetSystem& assets, ModelAsset& asset);

} // namespace Swim::Assets::SassetDetail
