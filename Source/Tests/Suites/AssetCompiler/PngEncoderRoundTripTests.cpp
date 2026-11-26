#include "Engine/Systems/Tooling/ImageEncoding.h"
#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The tool server's PNG encoder, read back by the asset compiler's independent decoder.
SWIM_TEST("Tooling.Images", "EncodedPngDecodesToTheSamePixels")
{
	using namespace Swim::AssetCompiler;
	Swim::Tooling::Image image{ 37, 23, {} };
	std::uint32_t seed = 12345;

	for (std::uint32_t y = 0; y < image.Height; ++y)
	{
		for (std::uint32_t x = 0; x < image.Width; ++x)
		{
			// Gradients (filters and matches) mixed with noise (literals).
			seed = seed * 1664525u + 1013904223u;
			const bool noisy = (x / 8 + y / 8) % 2 == 0;
			image.Rgba.push_back(static_cast<std::uint8_t>(noisy ? seed >> 24 : x * 6));
			image.Rgba.push_back(static_cast<std::uint8_t>(noisy ? seed >> 16 : y * 9));
			image.Rgba.push_back(static_cast<std::uint8_t>(noisy ? seed >> 8 : 128));
			image.Rgba.push_back(static_cast<std::uint8_t>(x == 0 ? 0 : 255));
		}
	}

	const std::vector<std::uint8_t> png = Swim::Tooling::EncodePng(image);
	const auto bytes = std::as_bytes(std::span(png));
	const SourceImageTextureCompileResult decoded = CompileSourceImageTexture(bytes, SourceImageMimeType::Png,
		Swim::Assets::TextureColorSpace::Linear, Swim::Assets::TextureSemantic::Color, CookedTextureEncoding::Rgba8);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(decoded), decoded.Error.Message);
	SWIM_REQUIRE(decoded.Asset.Width == image.Width && decoded.Asset.Height == image.Height);
	const auto& payload = decoded.Asset.Payloads[0];
	SWIM_REQUIRE(payload.Bytes.size() >= image.Rgba.size());
	std::size_t mismatches = 0;

	for (std::size_t i = 0; i < image.Rgba.size(); ++i)
	{
		mismatches += std::to_integer<std::uint8_t>(payload.Bytes[payload.Mips[0].OffsetBytes + i]) != image.Rgba[i];
	}

	SWIM_CHECK_EQUAL(mismatches, std::size_t{ 0 });
}
