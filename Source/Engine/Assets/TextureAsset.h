#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Swim::Assets
{

	enum class TextureDimension : std::uint8_t
	{
		Texture1D,
		Texture2D,
		Texture3D,
		Cube
	};

	enum class TextureColorSpace : std::uint8_t
	{
		Linear,
		SRgb
	};

	enum class TextureSemantic : std::uint8_t
	{
		Color,
		Normal,
		Data,
		HdrEnvironment
	};

	enum class TexturePayloadFormat : std::uint8_t
	{
		R8UNorm,
		RG8UNorm,
		RGBA8UNorm,
		RGBA8SRgb,
		RGBA16Float,
		BC1UNorm,
		BC1SRgb,
		BC3UNorm,
		BC3SRgb,
		BC5UNorm,
		BC7UNorm,
		BC7SRgb,
		ASTC4x4UNorm,
		ASTC4x4SRgb,
		ETC2RGBA8UNorm,
		ETC2RGBA8SRgb
	};

	struct TextureMipDesc
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Depth = 1;
		std::uint64_t OffsetBytes = 0;
		std::uint64_t SizeBytes = 0;
	};

	struct TexturePayloadVariant
	{
		TexturePayloadFormat Format = TexturePayloadFormat::RGBA8UNorm;
		std::vector<TextureMipDesc> Mips;
		std::vector<std::byte> Bytes;
	};

	struct TextureAsset
	{
		TextureDimension Dimension = TextureDimension::Texture2D;
		TextureColorSpace ColorSpace = TextureColorSpace::Linear;
		TextureSemantic Semantic = TextureSemantic::Color;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Depth = 1;
		std::uint32_t ArrayLayers = 1;
		std::vector<TexturePayloadVariant> Payloads;
	};

	enum class SamplerFilter : std::uint8_t
	{
		Nearest,
		Linear
	};

	enum class SamplerAddressMode : std::uint8_t
	{
		Repeat,
		MirroredRepeat,
		ClampToEdge,
		ClampToBorder
	};

	struct SamplerAsset
	{
		SamplerFilter MinFilter = SamplerFilter::Linear;
		SamplerFilter MagFilter = SamplerFilter::Linear;
		SamplerFilter MipFilter = SamplerFilter::Linear;
		SamplerAddressMode AddressU = SamplerAddressMode::Repeat;
		SamplerAddressMode AddressV = SamplerAddressMode::Repeat;
		SamplerAddressMode AddressW = SamplerAddressMode::Repeat;
		float MaxAnisotropy = 1.0f;
	};

}
