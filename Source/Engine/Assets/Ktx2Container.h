#pragma once

#include "Engine/Assets/TextureAsset.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Swim::Assets
{

	enum class Ktx2ErrorCode : std::uint8_t
	{
		None,
		Truncated,
		InvalidIdentifier,
		InvalidDimensions,
		InvalidFaceCount,
		InvalidLevelIndex,
		InvalidLevelData
	};

	struct Ktx2Error
	{
		Ktx2ErrorCode Code = Ktx2ErrorCode::None;
		std::string Message;
	};

	struct Ktx2Metadata
	{
		TextureDimension Dimension = TextureDimension::Texture2D;
		TextureSupercompression Supercompression = TextureSupercompression::None;
		TexturePayloadFormat PayloadFormat = TexturePayloadFormat::Undefined;
		TextureColorSpace ColorSpace = TextureColorSpace::Linear;
		bool HasDefinedColorSpace = false;
		std::uint32_t ContainerFormatCode = 0;
		std::uint32_t TypeSize = 1;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Depth = 1;
		std::uint32_t ArrayLayers = 1;
		std::uint32_t FaceCount = 1;
		std::uint32_t DeclaredLevelCount = 0;
		bool RequestsMipGeneration = false;
		std::vector<TextureMipDesc> Mips;
	};

	struct Ktx2ParseResult
	{
		Ktx2Metadata Metadata;
		Ktx2Error Error;

		explicit operator bool() const
		{
			return Error.Code == Ktx2ErrorCode::None;
		}
	};

	Ktx2ParseResult ParseKtx2Metadata(std::span<const std::byte> bytes);

}
