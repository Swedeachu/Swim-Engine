#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

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

	// A minimal uncompressed 2x2 RGBA8 KTX2 container.
	std::vector<std::byte> MakeUncompressedKtx2()
	{
		static constexpr std::array<std::byte, 12> Identifier
		{
			std::byte{ 0xAB }, std::byte{ 0x4B }, std::byte{ 0x54 }, std::byte{ 0x58 },
			std::byte{ 0x20 }, std::byte{ 0x32 }, std::byte{ 0x30 }, std::byte{ 0xBB },
			std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
		};

		std::vector<std::byte> bytes(108);
		std::copy(Identifier.begin(), Identifier.end(), bytes.begin());
		WriteU32(bytes, 12, 37);
		WriteU32(bytes, 16, 1);
		WriteU32(bytes, 20, 2);
		WriteU32(bytes, 24, 2);
		WriteU32(bytes, 36, 1);
		WriteU32(bytes, 40, 1);
		WriteU64(bytes, 80, 104);
		WriteU64(bytes, 88, 4);
		WriteU64(bytes, 96, 4);
		return bytes;
	}

}

SWIM_TEST("AssetCompiler.Ktx2TextureCompiler", "TypedFormatOverridesTheCallerColorSpaceHint")
{
	const std::vector<std::byte> bytes = MakeUncompressedKtx2();

	const Swim::AssetCompiler::Ktx2TextureCompileResult result = Swim::AssetCompiler::CompileKtx2Texture(
		bytes,
		Swim::Assets::TextureColorSpace::SRgb,
		Swim::Assets::TextureSemantic::Color);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);

	SWIM_CHECK_EQUAL(result.Asset.Width, 2u);
	SWIM_CHECK_EQUAL(result.Asset.Height, 2u);
	SWIM_CHECK(result.Asset.ColorSpace == Swim::Assets::TextureColorSpace::Linear);
}

SWIM_TEST("AssetCompiler.Ktx2TextureCompiler", "ValidatedContainerBytesArePassedThrough")
{
	const std::vector<std::byte> bytes = MakeUncompressedKtx2();

	const Swim::AssetCompiler::Ktx2TextureCompileResult result = Swim::AssetCompiler::CompileKtx2Texture(
		bytes,
		Swim::Assets::TextureColorSpace::SRgb,
		Swim::Assets::TextureSemantic::Color);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);

	SWIM_REQUIRE(result.Asset.Payloads.size() == 1);
	SWIM_CHECK(result.Asset.Payloads[0].Container == Swim::Assets::TextureContainerFormat::Ktx2);
	SWIM_CHECK(result.Asset.Payloads[0].Format == Swim::Assets::TexturePayloadFormat::RGBA8UNorm);
	SWIM_CHECK(result.Asset.Payloads[0].Bytes == bytes);
}
