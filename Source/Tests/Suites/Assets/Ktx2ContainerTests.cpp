#include "Engine/Assets/Ktx2Container.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{

	constexpr std::array<std::byte, 12> Ktx2Identifier
	{
		std::byte{ 0xAB }, std::byte{ 0x4B }, std::byte{ 0x54 }, std::byte{ 0x58 },
		std::byte{ 0x20 }, std::byte{ 0x32 }, std::byte{ 0x30 }, std::byte{ 0xBB },
		std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
	};

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

	std::vector<std::byte> MakeSingleMip2DKtx2()
	{
		std::vector<std::byte> bytes(120);
		std::copy(Ktx2Identifier.begin(), Ktx2Identifier.end(), bytes.begin());
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
		std::vector<std::byte> bytes(148);
		std::copy(Ktx2Identifier.begin(), Ktx2Identifier.end(), bytes.begin());
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

SWIM_TEST("Assets.Ktx2Container", "SingleMip2DMetadata")
{
	using namespace Swim::Assets;

	const std::vector<std::byte> bytes = MakeSingleMip2DKtx2();
	const Ktx2ParseResult result = ParseKtx2Metadata(bytes);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);

	SWIM_CHECK(result.Metadata.Dimension == TextureDimension::Texture2D);
	SWIM_CHECK_EQUAL(result.Metadata.Width, 4u);
	SWIM_CHECK_EQUAL(result.Metadata.Height, 4u);
	SWIM_CHECK_EQUAL(result.Metadata.Depth, 1u);
	SWIM_CHECK_EQUAL(result.Metadata.ArrayLayers, 1u);
	SWIM_CHECK(result.Metadata.Supercompression == TextureSupercompression::Zstandard);
	SWIM_CHECK_EQUAL(result.Metadata.ContainerFormatCode, 145u);
	SWIM_CHECK(result.Metadata.PayloadFormat == TexturePayloadFormat::BC7UNorm);
	SWIM_CHECK(result.Metadata.HasDefinedColorSpace);
	SWIM_CHECK(result.Metadata.ColorSpace == TextureColorSpace::Linear);

	SWIM_REQUIRE(result.Metadata.Mips.size() == 1);
	SWIM_CHECK_EQUAL(result.Metadata.Mips[0].OffsetBytes, std::uint64_t{ 104 });
	SWIM_CHECK_EQUAL(result.Metadata.Mips[0].SizeBytes, std::uint64_t{ 16 });
	SWIM_CHECK_EQUAL(result.Metadata.Mips[0].UncompressedSizeBytes, std::uint64_t{ 64 });
}

SWIM_TEST("Assets.Ktx2Container", "CubeArrayMipChainMetadata")
{
	using namespace Swim::Assets;

	const Ktx2ParseResult cubeArray = ParseKtx2Metadata(MakeCubeArrayKtx2());
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(cubeArray), cubeArray.Error.Message);

	SWIM_CHECK(cubeArray.Metadata.Dimension == TextureDimension::Cube);
	SWIM_CHECK_EQUAL(cubeArray.Metadata.ArrayLayers, 2u);
	SWIM_CHECK_EQUAL(cubeArray.Metadata.FaceCount, 6u);
	SWIM_REQUIRE(cubeArray.Metadata.Mips.size() == 2);
	SWIM_CHECK_EQUAL(cubeArray.Metadata.Mips[0].Width, 4u);
	SWIM_CHECK_EQUAL(cubeArray.Metadata.Mips[1].Width, 2u);
	SWIM_CHECK(cubeArray.Metadata.PayloadFormat == TexturePayloadFormat::BC7SRgb);
	SWIM_CHECK(cubeArray.Metadata.HasDefinedColorSpace);
	SWIM_CHECK(cubeArray.Metadata.ColorSpace == TextureColorSpace::SRgb);
}

SWIM_TEST("Assets.Ktx2Container", "MalformedContainersAreRejectedExplicitly")
{
	using namespace Swim::Assets;

	const std::vector<std::byte> bytes = MakeSingleMip2DKtx2();

	std::vector<std::byte> invalidIdentifier = bytes;
	invalidIdentifier[0] = std::byte{ 0 };
	SWIM_CHECK(ParseKtx2Metadata(invalidIdentifier).Error.Code == Ktx2ErrorCode::InvalidIdentifier);

	SWIM_CHECK(ParseKtx2Metadata(std::span<const std::byte>(bytes.data(), 79)).Error.Code == Ktx2ErrorCode::Truncated);

	std::vector<std::byte> outOfRangeLevel = bytes;
	WriteU64(outOfRangeLevel, 80, 1000);
	SWIM_CHECK(ParseKtx2Metadata(outOfRangeLevel).Error.Code == Ktx2ErrorCode::InvalidLevelData);
}
