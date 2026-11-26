#pragma once

#include "Engine/Systems/Tooling/Json.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Swim::Tooling
{

	// Tightly packed RGBA8, top row first.
	struct Image
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<std::uint8_t> Rgba;

		bool IsValid() const { return Width && Height && Rgba.size() == std::size_t(Width) * Height * 4; }
	};

	// A PNG (8-bit RGBA, per-row filters, fixed-Huffman deflate). Empty for an invalid image.
	std::vector<std::uint8_t> EncodePng(const Image& image);

	bool WritePng(const std::filesystem::path& path, const Image& image);

	std::string EncodeBase64(std::span<const std::uint8_t> bytes);

	// An MCP image content item ({ "type": "image", "data", "mimeType" }). A tool result
	// object may carry such items in a "$content" array; the server moves them into the
	// result's content after the text item.
	Json ImageContent(std::span<const std::uint8_t> png);

	// Tiles images into a grid, each scaled (bilinear) to fit a cell of cellWidth x
	// cellHeight with its aspect kept. Columns 0: about square. Gaps are filled with
	// background (RGBA, red in the low byte).
	Image TileImages(std::span<const Image> images, std::uint32_t cellWidth, std::uint32_t cellHeight, std::uint32_t columns = 0,
		std::uint32_t gap = 4, std::uint32_t background = 0xFF202020u);

} // namespace Swim::Tooling
