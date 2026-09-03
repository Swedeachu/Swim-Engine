#include "Engine/Assets/Ktx2Container.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Swim::Assets
{
	namespace
	{
		constexpr std::array<std::byte, 12> Ktx2Identifier
		{
			std::byte{ 0xAB }, std::byte{ 0x4B }, std::byte{ 0x54 }, std::byte{ 0x58 },
			std::byte{ 0x20 }, std::byte{ 0x32 }, std::byte{ 0x30 }, std::byte{ 0xBB },
			std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
		};
		constexpr std::size_t HeaderSize = 80;
		constexpr std::size_t LevelIndexSize = 24;

		Ktx2ParseResult MakeError(Ktx2ErrorCode code, const char* message)
		{
			Ktx2ParseResult result;
			result.Error.Code = code;
			result.Error.Message = message;
			return result;
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


		struct FormatInfo
		{
			TexturePayloadFormat Format = TexturePayloadFormat::Undefined;
			TextureColorSpace ColorSpace = TextureColorSpace::Linear;
			bool HasDefinedColorSpace = false;
		};

		FormatInfo GetFormatInfo(std::uint32_t vkFormat)
		{
			switch (vkFormat)
			{
			case 9: // VK_FORMAT_R8_UNORM
				return { TexturePayloadFormat::R8UNorm, TextureColorSpace::Linear, true };
			case 16: // VK_FORMAT_R8G8_UNORM
				return { TexturePayloadFormat::RG8UNorm, TextureColorSpace::Linear, true };
			case 37: // VK_FORMAT_R8G8B8A8_UNORM
				return { TexturePayloadFormat::RGBA8UNorm, TextureColorSpace::Linear, true };
			case 43: // VK_FORMAT_R8G8B8A8_SRGB
				return { TexturePayloadFormat::RGBA8SRgb, TextureColorSpace::SRgb, true };
			case 97: // VK_FORMAT_R16G16B16A16_SFLOAT
				return { TexturePayloadFormat::RGBA16Float, TextureColorSpace::Linear, true };
			case 133: // VK_FORMAT_BC1_RGBA_UNORM_BLOCK
				return { TexturePayloadFormat::BC1UNorm, TextureColorSpace::Linear, true };
			case 134: // VK_FORMAT_BC1_RGBA_SRGB_BLOCK
				return { TexturePayloadFormat::BC1SRgb, TextureColorSpace::SRgb, true };
			case 137: // VK_FORMAT_BC3_UNORM_BLOCK
				return { TexturePayloadFormat::BC3UNorm, TextureColorSpace::Linear, true };
			case 138: // VK_FORMAT_BC3_SRGB_BLOCK
				return { TexturePayloadFormat::BC3SRgb, TextureColorSpace::SRgb, true };
			case 141: // VK_FORMAT_BC5_UNORM_BLOCK
				return { TexturePayloadFormat::BC5UNorm, TextureColorSpace::Linear, true };
			case 145: // VK_FORMAT_BC7_UNORM_BLOCK
				return { TexturePayloadFormat::BC7UNorm, TextureColorSpace::Linear, true };
			case 146: // VK_FORMAT_BC7_SRGB_BLOCK
				return { TexturePayloadFormat::BC7SRgb, TextureColorSpace::SRgb, true };
			case 151: // VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK
				return { TexturePayloadFormat::ETC2RGBA8UNorm, TextureColorSpace::Linear, true };
			case 152: // VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK
				return { TexturePayloadFormat::ETC2RGBA8SRgb, TextureColorSpace::SRgb, true };
			case 157: // VK_FORMAT_ASTC_4x4_UNORM_BLOCK
				return { TexturePayloadFormat::ASTC4x4UNorm, TextureColorSpace::Linear, true };
			case 158: // VK_FORMAT_ASTC_4x4_SRGB_BLOCK
				return { TexturePayloadFormat::ASTC4x4SRgb, TextureColorSpace::SRgb, true };
			default:
				return {};
			}
		}

		TextureSupercompression ToSupercompression(std::uint32_t value)
		{
			switch (value)
			{
			case 0:
				return TextureSupercompression::None;
			case 1:
				return TextureSupercompression::BasisLz;
			case 2:
				return TextureSupercompression::Zstandard;
			case 3:
				return TextureSupercompression::Zlib;
			default:
				return TextureSupercompression::Unknown;
			}
		}

		std::uint32_t MipDimension(std::uint32_t value, std::size_t level)
		{
			value = std::max<std::uint32_t>(value, 1);
			for (std::size_t index = 0; index < level && value > 1; ++index)
			{
				value >>= 1;
			}
			return std::max<std::uint32_t>(value, 1);
		}

		bool RangeFits(std::uint64_t offset, std::uint64_t size, std::size_t totalSize)
		{
			if (offset > static_cast<std::uint64_t>(totalSize))
			{
				return false;
			}
			return size <= (static_cast<std::uint64_t>(totalSize) - offset);
		}
	}

	Ktx2ParseResult ParseKtx2Metadata(std::span<const std::byte> bytes)
	{
		if (bytes.size() < HeaderSize)
		{
			return MakeError(Ktx2ErrorCode::Truncated, "KTX2 header is truncated");
		}

		if (!std::equal(Ktx2Identifier.begin(), Ktx2Identifier.end(), bytes.begin()))
		{
			return MakeError(Ktx2ErrorCode::InvalidIdentifier, "payload does not contain the KTX2 file identifier");
		}

		Ktx2ParseResult result;
		Ktx2Metadata& metadata = result.Metadata;
		metadata.ContainerFormatCode = ReadU32(bytes, 12);
		const FormatInfo formatInfo = GetFormatInfo(metadata.ContainerFormatCode);
		metadata.PayloadFormat = formatInfo.Format;
		metadata.ColorSpace = formatInfo.ColorSpace;
		metadata.HasDefinedColorSpace = formatInfo.HasDefinedColorSpace;
		metadata.TypeSize = ReadU32(bytes, 16);
		metadata.Width = ReadU32(bytes, 20);
		const std::uint32_t pixelHeight = ReadU32(bytes, 24);
		const std::uint32_t pixelDepth = ReadU32(bytes, 28);
		const std::uint32_t layerCount = ReadU32(bytes, 32);
		metadata.FaceCount = ReadU32(bytes, 36);
		metadata.DeclaredLevelCount = ReadU32(bytes, 40);
		metadata.Supercompression = ToSupercompression(ReadU32(bytes, 44));

		if (metadata.Width == 0)
		{
			return MakeError(Ktx2ErrorCode::InvalidDimensions, "KTX2 pixelWidth must be non-zero");
		}
		if (metadata.FaceCount != 1 && metadata.FaceCount != 6)
		{
			return MakeError(Ktx2ErrorCode::InvalidFaceCount, "KTX2 faceCount must be one or six");
		}
		if (metadata.FaceCount == 6 && pixelDepth != 0)
		{
			return MakeError(Ktx2ErrorCode::InvalidDimensions, "KTX2 cubemap payload cannot be three-dimensional");
		}

		metadata.Height = std::max<std::uint32_t>(pixelHeight, 1);
		metadata.Depth = std::max<std::uint32_t>(pixelDepth, 1);
		metadata.ArrayLayers = std::max<std::uint32_t>(layerCount, 1);
		metadata.RequestsMipGeneration = metadata.DeclaredLevelCount == 0;
		if (metadata.FaceCount == 6)
		{
			metadata.Dimension = TextureDimension::Cube;
		}
		else if (pixelDepth != 0)
		{
			metadata.Dimension = TextureDimension::Texture3D;
		}
		else if (pixelHeight != 0)
		{
			metadata.Dimension = TextureDimension::Texture2D;
		}
		else
		{
			metadata.Dimension = TextureDimension::Texture1D;
		}

		const std::size_t levelCount = std::max<std::size_t>(metadata.DeclaredLevelCount, 1);
		if (levelCount > (std::numeric_limits<std::size_t>::max() - HeaderSize) / LevelIndexSize)
		{
			return MakeError(Ktx2ErrorCode::InvalidLevelIndex, "KTX2 level index size overflows the host address space");
		}
		const std::size_t levelIndexEnd = HeaderSize + levelCount * LevelIndexSize;
		if (levelIndexEnd > bytes.size())
		{
			return MakeError(Ktx2ErrorCode::InvalidLevelIndex, "KTX2 level index is truncated");
		}

		metadata.Mips.reserve(levelCount);
		for (std::size_t level = 0; level < levelCount; ++level)
		{
			const std::size_t offset = HeaderSize + level * LevelIndexSize;
			TextureMipDesc mip;
			mip.Width = MipDimension(metadata.Width, level);
			mip.Height = MipDimension(metadata.Height, level);
			mip.Depth = MipDimension(metadata.Depth, level);
			mip.OffsetBytes = ReadU64(bytes, offset + 0);
			mip.SizeBytes = ReadU64(bytes, offset + 8);
			mip.UncompressedSizeBytes = ReadU64(bytes, offset + 16);
			if (!RangeFits(mip.OffsetBytes, mip.SizeBytes, bytes.size()))
			{
				return MakeError(Ktx2ErrorCode::InvalidLevelData, "KTX2 mip level points outside the container payload");
			}
			if (metadata.Supercompression == TextureSupercompression::None && mip.SizeBytes != mip.UncompressedSizeBytes)
			{
				return MakeError(Ktx2ErrorCode::InvalidLevelData, "uncompressed KTX2 mip level has mismatched compressed/uncompressed sizes");
			}
			metadata.Mips.push_back(mip);
		}

		return result;
	}

}
