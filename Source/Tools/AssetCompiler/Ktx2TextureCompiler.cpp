#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"

#include <utility>

namespace Swim::AssetCompiler
{

	Ktx2TextureCompileResult CompileKtx2Texture(
		std::span<const std::byte> bytes,
		Swim::Assets::TextureColorSpace colorSpace,
		Swim::Assets::TextureSemantic semantic)
	{
		const Swim::Assets::Ktx2ParseResult parsed = Swim::Assets::ParseKtx2Metadata(bytes);
		if (!parsed)
		{
			Ktx2TextureCompileResult result;
			result.Error = parsed.Error;
			return result;
		}

		Ktx2TextureCompileResult result;
		result.Asset.Dimension = parsed.Metadata.Dimension;
		result.Asset.ColorSpace = parsed.Metadata.HasDefinedColorSpace ? parsed.Metadata.ColorSpace : colorSpace;
		result.Asset.Semantic = semantic;
		result.Asset.Width = parsed.Metadata.Width;
		result.Asset.Height = parsed.Metadata.Height;
		result.Asset.Depth = parsed.Metadata.Depth;
		result.Asset.ArrayLayers = parsed.Metadata.ArrayLayers;

		Swim::Assets::TexturePayloadVariant payload;
		payload.Container = Swim::Assets::TextureContainerFormat::Ktx2;
		payload.Format = parsed.Metadata.PayloadFormat;
		payload.Supercompression = parsed.Metadata.Supercompression;
		payload.ContainerFormatCode = parsed.Metadata.ContainerFormatCode;
		payload.Mips = parsed.Metadata.Mips;
		payload.Bytes.assign(bytes.begin(), bytes.end());
		result.Asset.Payloads.push_back(std::move(payload));
		return result;
	}

}
