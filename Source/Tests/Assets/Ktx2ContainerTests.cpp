#include "Engine/Assets/Ktx2Container.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "KTX2 metadata test failed: " << message << '\n';
			std::exit(1);
		}
	}

	void WriteU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
	{
		for (std::size_t index = 0; index < 4; ++index)
		{
			bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFu);
		}
	}

	void WriteU64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value)
	{
		WriteU32(bytes, offset, static_cast<std::uint32_t>(value));
		WriteU32(bytes, offset + 4, static_cast<std::uint32_t>(value >> 32));
	}

	std::vector<std::byte> MakeKtx2()
	{
		static constexpr std::array<std::byte, 12> Identifier
		{
			std::byte{ 0xAB }, std::byte{ 0x4B }, std::byte{ 0x54 }, std::byte{ 0x58 },
			std::byte{ 0x20 }, std::byte{ 0x32 }, std::byte{ 0x30 }, std::byte{ 0xBB },
			std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
		};
		std::vector<std::byte> bytes(120);
		std::copy(Identifier.begin(), Identifier.end(), bytes.begin());
		WriteU32(bytes, 12, 145); // VK_FORMAT_BC7_UNORM_BLOCK.
		WriteU32(bytes, 16, 1);
		WriteU32(bytes, 20, 4);
		WriteU32(bytes, 24, 4);
		WriteU32(bytes, 28, 0);
		WriteU32(bytes, 32, 0);
		WriteU32(bytes, 36, 1);
		WriteU32(bytes, 40, 1);
		WriteU32(bytes, 44, 2);
		WriteU64(bytes, 80, 104);
		WriteU64(bytes, 88, 16);
		WriteU64(bytes, 96, 64);
		return bytes;
	}
	std::vector<std::byte> MakeCubeArrayKtx2()
	{
		static constexpr std::array<std::byte, 12> Identifier
		{
			std::byte{ 0xAB }, std::byte{ 0x4B }, std::byte{ 0x54 }, std::byte{ 0x58 },
			std::byte{ 0x20 }, std::byte{ 0x32 }, std::byte{ 0x30 }, std::byte{ 0xBB },
			std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
		};
		std::vector<std::byte> bytes(148);
		std::copy(Identifier.begin(), Identifier.end(), bytes.begin());
		WriteU32(bytes, 12, 146); // VK_FORMAT_BC7_SRGB_BLOCK.
		WriteU32(bytes, 16, 1);
		WriteU32(bytes, 20, 4);
		WriteU32(bytes, 24, 4);
		WriteU32(bytes, 28, 0);
		WriteU32(bytes, 32, 2);
		WriteU32(bytes, 36, 6);
		WriteU32(bytes, 40, 2);
		WriteU32(bytes, 44, 0);
		WriteU64(bytes, 80, 128);
		WriteU64(bytes, 88, 16);
		WriteU64(bytes, 96, 16);
		WriteU64(bytes, 104, 144);
		WriteU64(bytes, 112, 4);
		WriteU64(bytes, 120, 4);
		return bytes;
	}

}

int main()
{
	using namespace Swim::Assets;
	const std::vector<std::byte> bytes = MakeKtx2();
	const Ktx2ParseResult result = ParseKtx2Metadata(bytes);
	Require(static_cast<bool>(result), result.Error.Message.c_str());
	Require(result.Metadata.Dimension == TextureDimension::Texture2D, "2D texture dimension parsed");
	Require(result.Metadata.Width == 4 && result.Metadata.Height == 4, "base dimensions parsed");
	Require(result.Metadata.Depth == 1 && result.Metadata.ArrayLayers == 1, "zero KTX depth/layers normalized to one runtime unit");
	Require(result.Metadata.Supercompression == TextureSupercompression::Zstandard, "supercompression metadata parsed");
	Require(result.Metadata.ContainerFormatCode == 145, "container format code preserved without Vulkan API types");
	Require(result.Metadata.PayloadFormat == TexturePayloadFormat::BC7UNorm, "common KTX2 vkFormat mapped into backend-neutral texture format");
	Require(result.Metadata.HasDefinedColorSpace && result.Metadata.ColorSpace == TextureColorSpace::Linear, "linear color space inferred from typed KTX2 format");
	Require(result.Metadata.Mips.size() == 1, "level index parsed");
	Require(result.Metadata.Mips[0].OffsetBytes == 104, "level byte offset parsed");
	Require(result.Metadata.Mips[0].SizeBytes == 16, "level compressed size parsed");
	Require(result.Metadata.Mips[0].UncompressedSizeBytes == 64, "level uncompressed size parsed");

	const Ktx2ParseResult cubeArray = ParseKtx2Metadata(MakeCubeArrayKtx2());
	Require(static_cast<bool>(cubeArray), cubeArray.Error.Message.c_str());
	Require(cubeArray.Metadata.Dimension == TextureDimension::Cube, "cubemap dimension parsed");
	Require(cubeArray.Metadata.ArrayLayers == 2 && cubeArray.Metadata.FaceCount == 6, "cube-array metadata preserved");
	Require(cubeArray.Metadata.Mips.size() == 2, "full mip chain metadata parsed");
	Require(cubeArray.Metadata.Mips[0].Width == 4 && cubeArray.Metadata.Mips[1].Width == 2, "mip dimensions derived from level index");
	Require(cubeArray.Metadata.PayloadFormat == TexturePayloadFormat::BC7SRgb, "sRGB compressed format mapped without backend API types");
	Require(cubeArray.Metadata.HasDefinedColorSpace && cubeArray.Metadata.ColorSpace == TextureColorSpace::SRgb, "sRGB color space inferred from typed KTX2 format");

	std::vector<std::byte> invalid = bytes;
	invalid[0] = std::byte{ 0 };
	Require(ParseKtx2Metadata(invalid).Error.Code == Ktx2ErrorCode::InvalidIdentifier, "identifier validation");
	Require(ParseKtx2Metadata(std::span<const std::byte>(bytes.data(), 79)).Error.Code == Ktx2ErrorCode::Truncated, "truncated header validation");

	invalid = bytes;
	WriteU64(invalid, 80, 1000);
	Require(ParseKtx2Metadata(invalid).Error.Code == Ktx2ErrorCode::InvalidLevelData, "out-of-range level rejected");
	return 0;
}
