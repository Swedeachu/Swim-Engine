#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Swim::AssetCompiler
{
	// How the cooker stores native mip chains. BC7 (4 x 4 blocks, 16 bytes: a quarter of
	// RGBA8) is what cooked models use; RGBA8 remains for tools and tests that inspect
	// texels.
	enum class CookedTextureEncoding : std::uint8_t
	{
		Bc7,
		Rgba8
	};

	// Bytes of a BC7 image (whole 4 x 4 blocks, partial edge blocks included).
	constexpr std::uint64_t GetBc7Bytes(std::uint32_t width, std::uint32_t height)
	{
		return ((std::uint64_t(width) + 3) / 4) * ((std::uint64_t(height) + 3) / 4) * 16u;
	}

	// Encodes a tightly packed RGBA8 image into row-major BC7 blocks (edge blocks repeat the
	// last column/row). Each block goes through UASTC and the transcoder's UASTC -> BC7 step;
	// block rows are spread over the machine's cores.
	std::vector<std::byte> EncodeRgba8ToBc7(std::span<const std::byte> rgba, std::uint32_t width, std::uint32_t height);

	// Decodes BC7 blocks back to RGBA8 (tests and diagnostics).
	std::vector<std::byte> DecodeBc7ToRgba8(std::span<const std::byte> blocks, std::uint32_t width, std::uint32_t height);
} // namespace Swim::AssetCompiler
