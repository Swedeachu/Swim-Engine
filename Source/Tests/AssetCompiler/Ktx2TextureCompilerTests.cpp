#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "KTX2 compiler test failed: " << message << '\n';
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
}

int main()
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

	const Swim::AssetCompiler::Ktx2TextureCompileResult result = Swim::AssetCompiler::CompileKtx2Texture(
		bytes,
		Swim::Assets::TextureColorSpace::SRgb,
		Swim::Assets::TextureSemantic::Color);
	Require(static_cast<bool>(result), result.Error.Message.c_str());
	Require(result.Asset.Width == 2 && result.Asset.Height == 2, "texture dimensions propagated");
	Require(result.Asset.ColorSpace == Swim::Assets::TextureColorSpace::Linear, "typed KTX2 format overrides an incompatible caller color-space hint");
	Require(result.Asset.Payloads.size() == 1, "one KTX2 payload emitted");
	Require(result.Asset.Payloads[0].Container == Swim::Assets::TextureContainerFormat::Ktx2, "KTX2 container identity preserved");
	Require(result.Asset.Payloads[0].Format == Swim::Assets::TexturePayloadFormat::RGBA8UNorm, "KTX2 vkFormat mapped to backend-neutral payload format");
	Require(result.Asset.Payloads[0].Bytes == bytes, "compiled payload retains validated KTX2 bytes");
	return 0;
}
