#pragma once

#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

#include <cstdint>

namespace Swim::Rhi
{

	// Bytes per texel for tightly packed, uncompressed color formats. Returns zero
	// for depth/stencil, block-compressed and undefined formats, whose buffer/image
	// transfers need separate block/aspect contracts.
	constexpr std::uint32_t GetUncompressedColorTexelBytes(Format format)
	{
		switch (format)
		{
		case Format::R8Unorm: case Format::R8Snorm: case Format::R8Uint: case Format::R8Sint:
			return 1;
		case Format::R16Unorm: case Format::R16Snorm: case Format::R16Uint: case Format::R16Sint: case Format::R16Float:
		case Format::RG8Unorm: case Format::RG8Snorm: case Format::RG8Uint: case Format::RG8Sint:
			return 2;
		case Format::R32Uint: case Format::R32Sint: case Format::R32Float:
		case Format::RG16Unorm: case Format::RG16Snorm: case Format::RG16Uint: case Format::RG16Sint: case Format::RG16Float:
		case Format::RGBA8Unorm: case Format::RGBA8UnormSrgb: case Format::RGBA8Snorm: case Format::RGBA8Uint: case Format::RGBA8Sint:
		case Format::BGRA8Unorm: case Format::BGRA8UnormSrgb:
		case Format::BGR10A2Unorm: case Format::RGB10A2Unorm: case Format::RGB10A2Uint: case Format::R11G11B10Float: case Format::RGB9E5Float:
			return 4;
		case Format::RG32Uint: case Format::RG32Sint: case Format::RG32Float:
		case Format::RGBA16Unorm: case Format::RGBA16Snorm: case Format::RGBA16Uint: case Format::RGBA16Sint: case Format::RGBA16Float:
			return 8;
		case Format::RGB32Uint: case Format::RGB32Sint: case Format::RGB32Float:
			return 12;
		case Format::RGBA32Uint: case Format::RGBA32Sint: case Format::RGBA32Float:
			return 16;
		default:
			return 0;
		}
	}

	// Bytes per texel of a buffer/image transfer: the color formats above plus
	// D32Float, whose only aspect (depth) copies as tightly packed 32-bit floats
	// (for example a shadow atlas read back for verification). Zero otherwise:
	// packed depth/stencil formats need a per-aspect contract.
	constexpr std::uint32_t GetTransferTexelBytes(Format format)
	{
		return format == Format::D32Float ? 4u : GetUncompressedColorTexelBytes(format);
	}

} // namespace Swim::Rhi
