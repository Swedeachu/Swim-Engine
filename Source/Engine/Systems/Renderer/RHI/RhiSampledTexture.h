#pragma once

#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

namespace Swim::Rhi
{

	constexpr bool IsSampledTextureDimension(TextureViewDimension dimension)
	{
		switch (dimension)
		{
		case TextureViewDimension::Texture1D: case TextureViewDimension::Texture1DArray:
		case TextureViewDimension::Texture2D: case TextureViewDimension::Texture2DArray:
		case TextureViewDimension::Texture3D: case TextureViewDimension::TextureCube:
		case TextureViewDimension::TextureCubeArray:
			return true;
		default: return false;
		}
	}

	// Depth/stencil and unknown formats are outside the sampled-color contract.
	constexpr SampledTextureClass GetSampledTextureClass(Format format)
	{
		switch (format)
		{
		case Format::R8Unorm:
		case Format::R8Snorm:
		case Format::R16Unorm:
		case Format::R16Snorm:
		case Format::R16Float:
		case Format::R32Float:
		case Format::RG8Unorm:
		case Format::RG8Snorm:
		case Format::RG16Unorm:
		case Format::RG16Snorm:
		case Format::RG16Float:
		case Format::RG32Float:
		case Format::RGB32Float:
		case Format::RGBA8Unorm:
		case Format::RGBA8UnormSrgb:
		case Format::RGBA8Snorm:
		case Format::BGRA8Unorm:
		case Format::BGRA8UnormSrgb:
		case Format::RGBA16Unorm:
		case Format::RGBA16Snorm:
		case Format::RGBA16Float:
		case Format::RGBA32Float:
		case Format::RGB10A2Unorm:
		case Format::R11G11B10Float:
		case Format::RGB9E5Float:
		case Format::BC1RGBAUnorm:
		case Format::BC1RGBAUnormSrgb:
		case Format::BC3Unorm:
		case Format::BC3UnormSrgb:
		case Format::BC4Unorm:
		case Format::BC4Snorm:
		case Format::BC5Unorm:
		case Format::BC5Snorm:
		case Format::BC6HUfloat:
		case Format::BC6HSfloat:
		case Format::BC7Unorm:
		case Format::BC7UnormSrgb:
		case Format::ETC2RGB8Unorm:
		case Format::ETC2RGB8UnormSrgb:
		case Format::ETC2RGBA8Unorm:
		case Format::ETC2RGBA8UnormSrgb:
		case Format::ASTC4x4Unorm:
		case Format::ASTC4x4UnormSrgb:
		case Format::ASTC6x6Unorm:
		case Format::ASTC6x6UnormSrgb:
		case Format::ASTC8x8Unorm:
		case Format::ASTC8x8UnormSrgb:
		case Format::BGR10A2Unorm:
			return SampledTextureClass::Float;
		case Format::R8Uint:
		case Format::R16Uint:
		case Format::R32Uint:
		case Format::RG8Uint:
		case Format::RG16Uint:
		case Format::RG32Uint:
		case Format::RGB32Uint:
		case Format::RGBA8Uint:
		case Format::RGBA16Uint:
		case Format::RGBA32Uint:
		case Format::RGB10A2Uint:
			return SampledTextureClass::Uint;
		case Format::R8Sint:
		case Format::R16Sint:
		case Format::R32Sint:
		case Format::RG8Sint:
		case Format::RG16Sint:
		case Format::RG32Sint:
		case Format::RGB32Sint:
		case Format::RGBA8Sint:
		case Format::RGBA16Sint:
		case Format::RGBA32Sint:
			return SampledTextureClass::Sint;
		default: return SampledTextureClass::Undefined;
		}
	}

} // namespace Swim::Rhi
