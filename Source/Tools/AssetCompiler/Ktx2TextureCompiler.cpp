#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"

#include <basisu_transcoder.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace Swim::AssetCompiler
{
	namespace
	{
		// KTX2 header fields (little endian) the Basis check needs.
		std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset)
		{
			std::uint32_t value = 0;
			for (std::size_t i = 0; i < 4; ++i)
			{
				value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
			}
			return value;
		}

		// KHR_texture_basisu payloads: VK_FORMAT_UNDEFINED with BasisLZ (ETC1S) or a UASTC
		// data format descriptor. Everything else keeps the validated container as is.
		bool IsBasisUniversal(std::span<const std::byte> bytes)
		{
			constexpr std::size_t HeaderBytes = 80;
			if (bytes.size() < HeaderBytes || ReadU32(bytes, 12) != 0u)
			{
				return false;
			}
			constexpr std::uint32_t BasisLz = 1;
			if (ReadU32(bytes, 44) == BasisLz)
			{
				return true;
			}
			// DFD: dfdTotalSize (u32), then the basic block; its colour model is byte 8 of the block.
			const std::uint32_t dfdOffset = ReadU32(bytes, 48);
			constexpr std::uint32_t ColorModelUastc = 166;
			return dfdOffset != 0 && std::size_t(dfdOffset) + 12 < bytes.size() &&
				static_cast<std::uint32_t>(bytes[dfdOffset + 4 + 8]) == ColorModelUastc;
		}

		Ktx2TextureCompileResult Fail(Swim::Assets::Ktx2ErrorCode code, std::string message)
		{
			Ktx2TextureCompileResult result;
			result.Error.Code = code;
			result.Error.Message = std::move(message);
			return result;
		}

		// Transcodes every mip of a 2D Basis Universal KTX2 into a tightly packed RGBA8 native
		// mip chain (sRGB when the file's transfer function is sRGB), which every GPU and the
		// runtime's texture residency can upload directly.
		Ktx2TextureCompileResult TranscodeBasis(std::span<const std::byte> bytes, Swim::Assets::TextureSemantic semantic)
		{
			static std::once_flag initialized;
			std::call_once(initialized,
				[]
				{
					basist::basisu_transcoder_init();
				});

			if (bytes.size() > std::numeric_limits<std::uint32_t>::max())
			{
				return Fail(Swim::Assets::Ktx2ErrorCode::InvalidLevelData, "Basis KTX2 texture is larger than 4 GiB");
			}
			basist::ktx2_transcoder transcoder;
			if (!transcoder.init(bytes.data(), static_cast<std::uint32_t>(bytes.size())))
			{
				return Fail(Swim::Assets::Ktx2ErrorCode::InvalidLevelData, "Basis KTX2 header or level index is invalid");
			}
			if (transcoder.get_faces() != 1 || transcoder.get_layers() > 1 || !transcoder.is_ldr())
			{
				return Fail(Swim::Assets::Ktx2ErrorCode::InvalidDimensions,
					"only single-layer 2D LDR Basis KTX2 textures are supported (no cube maps, arrays or HDR)");
			}
			if (!transcoder.start_transcoding())
			{
				return Fail(Swim::Assets::Ktx2ErrorCode::InvalidLevelData,
					"Basis KTX2 transcoding could not start (Zstandard-supercompressed UASTC is not supported)");
			}

			const bool srgb = transcoder.get_dfd_transfer_func() == basist::KTX2_KHR_DF_TRANSFER_SRGB;
			Ktx2TextureCompileResult result;
			auto& asset = result.Asset;
			asset.Dimension = Swim::Assets::TextureDimension::Texture2D;
			asset.ColorSpace = srgb ? Swim::Assets::TextureColorSpace::SRgb : Swim::Assets::TextureColorSpace::Linear;
			asset.Semantic = semantic;
			asset.Width = transcoder.get_width();
			asset.Height = transcoder.get_height();
			asset.Depth = 1;
			asset.ArrayLayers = 1;

			Swim::Assets::TexturePayloadVariant payload;
			payload.Container = Swim::Assets::TextureContainerFormat::NativeMipData;
			payload.Format = srgb ? Swim::Assets::TexturePayloadFormat::RGBA8SRgb : Swim::Assets::TexturePayloadFormat::RGBA8UNorm;
			payload.Supercompression = Swim::Assets::TextureSupercompression::None;
			const std::uint32_t levels = std::max(1u, transcoder.get_levels());
			for (std::uint32_t level = 0; level < levels; ++level)
			{
				basist::ktx2_image_level_info info{};
				if (!transcoder.get_image_level_info(info, level, 0, 0))
				{
					return Fail(
						Swim::Assets::Ktx2ErrorCode::InvalidLevelIndex, "Basis KTX2 level " + std::to_string(level) + " is missing");
				}
				const std::uint32_t width = std::max(1u, asset.Width >> level);
				const std::uint32_t height = std::max(1u, asset.Height >> level);
				if (info.m_orig_width != width || info.m_orig_height != height)
				{
					return Fail(Swim::Assets::Ktx2ErrorCode::InvalidDimensions,
						"Basis KTX2 level " + std::to_string(level) + " is not a regular mip chain level");
				}
				const std::uint64_t size = std::uint64_t(width) * height * 4u;
				const std::uint64_t offset = payload.Bytes.size();
				payload.Bytes.resize(static_cast<std::size_t>(offset + size));
				if (!transcoder.transcode_image_level(
						level, 0, 0, payload.Bytes.data() + offset, width * height, basist::transcoder_texture_format::cTFRGBA32))
				{
					return Fail(Swim::Assets::Ktx2ErrorCode::InvalidLevelData,
						"Basis KTX2 level " + std::to_string(level) + " failed to transcode");
				}
				payload.Mips.push_back({ width, height, 1, offset, size, size });
			}
			asset.Payloads.push_back(std::move(payload));
			return result;
		}
	} // namespace

	Ktx2TextureCompileResult CompileKtx2Texture(
		std::span<const std::byte> bytes, Swim::Assets::TextureColorSpace colorSpace, Swim::Assets::TextureSemantic semantic)
	{
		const Swim::Assets::Ktx2ParseResult parsed = Swim::Assets::ParseKtx2Metadata(bytes);
		if (!parsed)
		{
			Ktx2TextureCompileResult result;
			result.Error = parsed.Error;
			return result;
		}
		if (IsBasisUniversal(bytes))
		{
			return TranscodeBasis(bytes, semantic);
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

} // namespace Swim::AssetCompiler
