#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "Source image texture compiler test failed: " << message << '\n';
			std::exit(1);
		}
	}

	static constexpr std::array<std::byte, 74> PngBytes
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

	static constexpr std::array<std::byte, 40> WebPBytes
	{
		std::byte{ 0x52 }, std::byte{ 0x49 }, std::byte{ 0x46 }, std::byte{ 0x46 }, std::byte{ 0x20 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
		std::byte{ 0x57 }, std::byte{ 0x45 }, std::byte{ 0x42 }, std::byte{ 0x50 }, std::byte{ 0x56 }, std::byte{ 0x50 }, std::byte{ 0x38 }, std::byte{ 0x4C },
		std::byte{ 0x13 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x2F }, std::byte{ 0x01 }, std::byte{ 0x00 }, std::byte{ 0x00 },
		std::byte{ 0x10 }, std::byte{ 0x0F }, std::byte{ 0x30 }, std::byte{ 0xFF }, std::byte{ 0xFB }, std::byte{ 0x1F }, std::byte{ 0x0F }, std::byte{ 0xFC },
		std::byte{ 0x0F }, std::byte{ 0x07 }, std::byte{ 0x15 }, std::byte{ 0x88 }, std::byte{ 0xE8 }, std::byte{ 0x7F }, std::byte{ 0x00 }, std::byte{ 0x00 }
	};
}

int main()
{
	using namespace Swim::AssetCompiler;

	Require(DetectSourceImageMimeType(PngBytes) == SourceImageMimeType::Png, "PNG magic detection");
	Require(DetectSourceImageMimeType(WebPBytes) == SourceImageMimeType::WebP, "WebP magic detection");
	static constexpr std::array<std::byte, 3> JpegMagic
	{
		std::byte{ 0xFF }, std::byte{ 0xD8 }, std::byte{ 0xFF }
	};
	Require(DetectSourceImageMimeType(JpegMagic) == SourceImageMimeType::Jpeg, "JPEG magic detection");

	const SourceImageTextureCompileResult png = CompileSourceImageTexture(
		PngBytes,
		SourceImageMimeType::Unknown,
		Swim::Assets::TextureColorSpace::SRgb,
		Swim::Assets::TextureSemantic::Color);
	Require(static_cast<bool>(png), png.Error.Message.c_str());
	Require(png.Asset.Width == 2 && png.Asset.Height == 1, "PNG dimensions");
	Require(png.Asset.Payloads.size() == 1, "PNG payload count");
	Require(png.Asset.Payloads[0].Container == Swim::Assets::TextureContainerFormat::NativeMipData, "PNG cooks to native RGBA payload");
	Require(png.Asset.Payloads[0].Format == Swim::Assets::TexturePayloadFormat::RGBA8SRgb, "PNG color space is preserved");
	Require(png.Asset.Payloads[0].Mips.size() == 2, "PNG compiler generates a full mip chain");
	Require(png.Asset.Payloads[0].Mips[0].Width == 2 && png.Asset.Payloads[0].Mips[0].Height == 1, "PNG base mip dimensions");
	Require(png.Asset.Payloads[0].Mips[1].Width == 1 && png.Asset.Payloads[0].Mips[1].Height == 1, "PNG tail mip dimensions");
	Require(png.Asset.Payloads[0].Bytes.size() == 12, "PNG cooks base and tail RGBA8 mip payloads");
	Require(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[8]) == 188, "sRGB mip generation averages red in linear space");
	Require(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[9]) == 188, "sRGB mip generation averages green in linear space");
	Require(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[10]) == 0, "sRGB mip generation preserves zero blue");
	Require(std::to_integer<std::uint8_t>(png.Asset.Payloads[0].Bytes[11]) == 192, "mip generation averages alpha linearly");

	const SourceImageTextureCompileResult normal = CompileSourceImageTexture(
		PngBytes,
		SourceImageMimeType::Png,
		Swim::Assets::TextureColorSpace::Linear,
		Swim::Assets::TextureSemantic::Normal);
	Require(static_cast<bool>(normal), normal.Error.Message.c_str());
	Require(normal.Asset.Payloads[0].Mips.size() == 2, "normal map compiler generates a full mip chain");
	Require(std::to_integer<std::uint8_t>(normal.Asset.Payloads[0].Bytes[8]) == 128, "normal mip renormalizes X");
	Require(std::to_integer<std::uint8_t>(normal.Asset.Payloads[0].Bytes[9]) == 128, "normal mip renormalizes Y");
	Require(std::to_integer<std::uint8_t>(normal.Asset.Payloads[0].Bytes[10]) == 0, "normal mip renormalizes Z");

	const SourceImageTextureCompileResult webp = CompileSourceImageTexture(
		WebPBytes,
		SourceImageMimeType::WebP,
		Swim::Assets::TextureColorSpace::Linear,
		Swim::Assets::TextureSemantic::Data);
	Require(static_cast<bool>(webp), webp.Error.Message.c_str());
	Require(webp.Asset.Width == 2 && webp.Asset.Height == 1, "WebP dimensions");
	Require(webp.Asset.Payloads[0].Format == Swim::Assets::TexturePayloadFormat::RGBA8UNorm, "WebP linear color space is preserved");
	Require(webp.Asset.Payloads[0].Mips.size() == 2, "WebP compiler generates a full mip chain");
	Require(webp.Asset.Payloads[0].Bytes.size() == 12, "WebP cooks base and tail RGBA8 mip payloads");

	static constexpr std::array<std::byte, 4> Unsupported
	{
		std::byte{ 0x44 }, std::byte{ 0x44 }, std::byte{ 0x53 }, std::byte{ 0x20 }
	};
	const SourceImageTextureCompileResult unsupported = CompileSourceImageTexture(
		Unsupported,
		SourceImageMimeType::Unknown,
		Swim::Assets::TextureColorSpace::Linear,
		Swim::Assets::TextureSemantic::Data);
	Require(!unsupported && unsupported.Error.Code == SourceImageTextureCompileErrorCode::UnsupportedSource, "DDS remains an explicit unsupported source path");
	return 0;
}
