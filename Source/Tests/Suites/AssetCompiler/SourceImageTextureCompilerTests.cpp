#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

	// A 2x1 RGBA PNG and a 2x1 lossless WebP, small enough to keep inline.
	constexpr std::array<std::byte, 74> PngBytes
	{
		std::byte{ 0x89 }, std::byte{ 0x50 }, std::byte{ 0x4E }, std::byte{ 0x47 }, std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A },
		std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x0D }, std::byte{ 0x49 }, std::byte{ 0x48 }, std::byte{ 0x44 }, std::byte{ 0x52 },
		std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x02 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x01 },
		std::byte{ 0x08 }, std::byte{ 0x06 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0xF4 }, std::byte{ 0x22 }, std::byte{ 0x7F },
		std::byte{ 0x8A }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x11 }, std::byte{ 0x49 }, std::byte{ 0x44 }, std::byte{ 0x41 },
		std::byte{ 0x54 }, std::byte{ 0x78 }, std::byte{ 0x9C }, std::byte{ 0x63 }, std::byte{ 0xF8 }, std::byte{ 0xCF }, std::byte{ 0xC0 }, std::byte{ 0xF0 },
		std::byte{ 0x9F }, std::byte{ 0xE1 }, std::byte{ 0x3F }, std::byte{ 0x43 }, std::byte{ 0x03 }, std::byte{ 0x00 }, std::byte{ 0x10 }, std::byte{ 0x79 },
		std::byte{ 0x03 }, std::byte{ 0x7E }, std::byte{ 0x21 }, std::byte{ 0xC0 }, std::byte{ 0xFD }, std::byte{ 0x8D }, std::byte{ 0x00 }, std::byte{ 0x00 },
		std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x49 }, std::byte{ 0x45 }, std::byte{ 0x4E }, std::byte{ 0x44 }, std::byte{ 0xAE }, std::byte{ 0x42 },
		std::byte{ 0x60 }, std::byte{ 0x82 }
	};

	constexpr std::array<std::byte, 40> WebPBytes
	{
		std::byte{ 0x52 }, std::byte{ 0x49 }, std::byte{ 0x46 }, std::byte{ 0x46 }, std::byte{ 0x20 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
		std::byte{ 0x57 }, std::byte{ 0x45 }, std::byte{ 0x42 }, std::byte{ 0x50 }, std::byte{ 0x56 }, std::byte{ 0x50 }, std::byte{ 0x38 }, std::byte{ 0x4C },
		std::byte{ 0x13 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x2F }, std::byte{ 0x01 }, std::byte{ 0x00 }, std::byte{ 0x00 },
		std::byte{ 0x10 }, std::byte{ 0x0F }, std::byte{ 0x30 }, std::byte{ 0xFF }, std::byte{ 0xFB }, std::byte{ 0x1F }, std::byte{ 0x0F }, std::byte{ 0xFC },
		std::byte{ 0x0F }, std::byte{ 0x07 }, std::byte{ 0x15 }, std::byte{ 0x88 }, std::byte{ 0xE8 }, std::byte{ 0x7F }, std::byte{ 0x00 }, std::byte{ 0x00 }
	};

}

SWIM_TEST("AssetCompiler.SourceImageTextureCompiler", "DetectsSupportedSourceFormatsByMagic")
{
	using namespace Swim::AssetCompiler;

	static constexpr std::array<std::byte, 3> JpegMagic
	{
		std::byte{ 0xFF }, std::byte{ 0xD8 }, std::byte{ 0xFF }
	};

	SWIM_CHECK(DetectSourceImageMimeType(PngBytes) == SourceImageMimeType::Png);
	SWIM_CHECK(DetectSourceImageMimeType(WebPBytes) == SourceImageMimeType::WebP);
	SWIM_CHECK(DetectSourceImageMimeType(JpegMagic) == SourceImageMimeType::Jpeg);
}

SWIM_TEST("AssetCompiler.SourceImageTextureCompiler", "PngCooksToANativeSRgbMipChain")
{
	using namespace Swim::AssetCompiler;

	const SourceImageTextureCompileResult png = CompileSourceImageTexture(
		PngBytes,
		SourceImageMimeType::Unknown,
		Swim::Assets::TextureColorSpace::SRgb,
		Swim::Assets::TextureSemantic::Color);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(png), png.Error.Message);

	SWIM_CHECK_EQUAL(png.Asset.Width, 2u);
	SWIM_CHECK_EQUAL(png.Asset.Height, 1u);
	SWIM_REQUIRE(png.Asset.Payloads.size() == 1);
	SWIM_CHECK(png.Asset.Payloads[0].Container == Swim::Assets::TextureContainerFormat::NativeMipData);
	SWIM_CHECK(png.Asset.Payloads[0].Format == Swim::Assets::TexturePayloadFormat::RGBA8SRgb);

	SWIM_REQUIRE(png.Asset.Payloads[0].Mips.size() == 2);
	SWIM_CHECK_EQUAL(png.Asset.Payloads[0].Mips[0].Width, 2u);
	SWIM_CHECK_EQUAL(png.Asset.Payloads[0].Mips[0].Height, 1u);
	SWIM_CHECK_EQUAL(png.Asset.Payloads[0].Mips[1].Width, 1u);
	SWIM_CHECK_EQUAL(png.Asset.Payloads[0].Mips[1].Height, 1u);

	// sRGB sources must be averaged in linear space, and alpha averaged linearly.
	SWIM_REQUIRE(png.Asset.Payloads[0].Bytes.size() == 12);
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[8]), std::uint8_t{ 188 });
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[9]), std::uint8_t{ 188 });
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[10]), std::uint8_t{ 0 });
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[11]), std::uint8_t{ 192 });
}

SWIM_TEST("AssetCompiler.SourceImageTextureCompiler", "NormalMapMipsAreRenormalized")
{
	using namespace Swim::AssetCompiler;

	const SourceImageTextureCompileResult normal = CompileSourceImageTexture(
		PngBytes,
		SourceImageMimeType::Png,
		Swim::Assets::TextureColorSpace::Linear,
		Swim::Assets::TextureSemantic::Normal);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(normal), normal.Error.Message);

	SWIM_REQUIRE(normal.Asset.Payloads.size() == 1);
	SWIM_REQUIRE(normal.Asset.Payloads[0].Mips.size() == 2);
	SWIM_REQUIRE(normal.Asset.Payloads[0].Bytes.size() == 12);
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(normal.Asset.Payloads[0].Bytes[8]), std::uint8_t{ 128 });
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(normal.Asset.Payloads[0].Bytes[9]), std::uint8_t{ 128 });
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(normal.Asset.Payloads[0].Bytes[10]), std::uint8_t{ 0 });
}

SWIM_TEST("AssetCompiler.SourceImageTextureCompiler", "WebPCooksToALinearMipChain")
{
	using namespace Swim::AssetCompiler;

	const SourceImageTextureCompileResult webp = CompileSourceImageTexture(
		WebPBytes,
		SourceImageMimeType::WebP,
		Swim::Assets::TextureColorSpace::Linear,
		Swim::Assets::TextureSemantic::Data);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(webp), webp.Error.Message);

	SWIM_CHECK_EQUAL(webp.Asset.Width, 2u);
	SWIM_CHECK_EQUAL(webp.Asset.Height, 1u);
	SWIM_REQUIRE(webp.Asset.Payloads.size() == 1);
	SWIM_CHECK(webp.Asset.Payloads[0].Format == Swim::Assets::TexturePayloadFormat::RGBA8UNorm);
	SWIM_CHECK_EQUAL(webp.Asset.Payloads[0].Mips.size(), std::size_t{ 2 });
	SWIM_CHECK_EQUAL(webp.Asset.Payloads[0].Bytes.size(), std::size_t{ 12 });
}

SWIM_TEST("AssetCompiler.SourceImageTextureCompiler", "UnsupportedSourcesFailExplicitly")
{
	using namespace Swim::AssetCompiler;

	static constexpr std::array<std::byte, 4> DdsMagic
	{
		std::byte{ 0x44 }, std::byte{ 0x44 }, std::byte{ 0x53 }, std::byte{ 0x20 }
	};

	const SourceImageTextureCompileResult unsupported = CompileSourceImageTexture(
		DdsMagic,
		SourceImageMimeType::Unknown,
		Swim::Assets::TextureColorSpace::Linear,
		Swim::Assets::TextureSemantic::Data);

	SWIM_CHECK(!static_cast<bool>(unsupported));
	SWIM_CHECK(unsupported.Error.Code == SourceImageTextureCompileErrorCode::UnsupportedSource);
}
