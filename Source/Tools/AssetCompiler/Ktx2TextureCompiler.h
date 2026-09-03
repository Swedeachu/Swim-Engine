#pragma once

#include "Engine/Assets/Ktx2Container.h"

#include <cstddef>
#include <span>

namespace Swim::AssetCompiler
{

	struct Ktx2TextureCompileResult
	{
		Swim::Assets::TextureAsset Asset;
		Swim::Assets::Ktx2Error Error;

		explicit operator bool() const
		{
			return Error.Code == Swim::Assets::Ktx2ErrorCode::None;
		}
	};

	Ktx2TextureCompileResult CompileKtx2Texture(
		std::span<const std::byte> bytes,
		Swim::Assets::TextureColorSpace colorSpace,
		Swim::Assets::TextureSemantic semantic);

}
