#pragma once

#include "Engine/Assets/TextureAsset.h"
#include "Tools/AssetCompiler/IntermediateModel.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Swim::AssetCompiler
{

	enum class SourceImageTextureCompileErrorCode : std::uint8_t
	{
		None,
		UnsupportedSource,
		InvalidData,
		Overflow
	};

	struct SourceImageTextureCompileError
	{
		SourceImageTextureCompileErrorCode Code = SourceImageTextureCompileErrorCode::None;
		std::string Message;
	};

	struct SourceImageTextureCompileResult
	{
		Swim::Assets::TextureAsset Asset;
		SourceImageTextureCompileError Error;

		explicit operator bool() const
		{
			return Error.Code == SourceImageTextureCompileErrorCode::None;
		}
	};

	SourceImageMimeType DetectSourceImageMimeType(
		std::span<const std::byte> bytes,
		SourceImageMimeType hint = SourceImageMimeType::Unknown);

	SourceImageTextureCompileResult CompileSourceImageTexture(
		std::span<const std::byte> bytes,
		SourceImageMimeType mimeType,
		Swim::Assets::TextureColorSpace colorSpace,
		Swim::Assets::TextureSemantic semantic);

}
