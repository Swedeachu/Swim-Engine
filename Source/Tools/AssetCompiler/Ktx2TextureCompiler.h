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

		explicit operator bool() const { return Error.Code == Swim::Assets::Ktx2ErrorCode::None; }
	};

	// Validates a KTX2 container. Basis Universal payloads (KHR_texture_basisu: ETC1S/BasisLZ or
	// UASTC) are transcoded here, at cook time, into an RGBA8 native mip chain (sRGB when the
	// file's transfer function is sRGB), so the runtime needs no transcoder. Other KTX2 files
	// keep their validated container bytes.
	Ktx2TextureCompileResult CompileKtx2Texture(
		std::span<const std::byte> bytes, Swim::Assets::TextureColorSpace colorSpace, Swim::Assets::TextureSemantic semantic);

} // namespace Swim::AssetCompiler
