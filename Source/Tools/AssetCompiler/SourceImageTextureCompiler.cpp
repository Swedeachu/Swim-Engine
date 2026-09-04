#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"

// The development compiler can be linked into the legacy runtime. Keep its
// PNG/JPEG decoder private to this TU so it cannot collide with the explicit
// runtime stb_image implementation used by still-loose compatibility assets.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <webp/decode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace Swim::AssetCompiler
{
	namespace
	{
		SourceImageTextureCompileResult MakeError(SourceImageTextureCompileErrorCode code, std::string message)
		{
			SourceImageTextureCompileResult result;
			result.Error.Code = code;
			result.Error.Message = std::move(message);
			return result;
		}

		bool HasPrefix(std::span<const std::byte> bytes, std::span<const std::byte> prefix)
		{
			return bytes.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), bytes.begin());
		}

		bool TryGetRgba8ByteCount(int width, int height, std::size_t& byteCount)
		{
			if (width <= 0 || height <= 0)
			{
				return false;
			}
			const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
			if (pixelCount > std::numeric_limits<std::size_t>::max() / 4u)
			{
				return false;
			}
			byteCount = static_cast<std::size_t>(pixelCount * 4u);
			return true;
		}

		bool DecodeStbRgba8(
			std::span<const std::byte> bytes,
			std::vector<std::byte>& rgba,
			int& width,
			int& height,
			std::string& error)
		{
			if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			{
				error = "PNG/JPEG source image is empty or exceeds stb_image's input-size limit";
				return false;
			}

			stbi_uc* decoded = stbi_load_from_memory(
				reinterpret_cast<const stbi_uc*>(bytes.data()),
				static_cast<int>(bytes.size()),
				&width,
				&height,
				nullptr,
				STBI_rgb_alpha
			);
			if (!decoded)
			{
				const char* reason = stbi_failure_reason();
				error = reason ? reason : "stb_image failed to decode source image";
				return false;
			}

			std::size_t byteCount = 0;
			if (!TryGetRgba8ByteCount(width, height, byteCount))
			{
				stbi_image_free(decoded);
				error = "decoded PNG/JPEG dimensions overflow RGBA8 storage";
				return false;
			}

			rgba.resize(byteCount);
			std::memcpy(rgba.data(), decoded, byteCount);
			stbi_image_free(decoded);
			return true;
		}

		bool DecodeWebPRgba8(
			std::span<const std::byte> bytes,
			std::vector<std::byte>& rgba,
			int& width,
			int& height,
			std::string& error)
		{
			if (bytes.empty())
			{
				error = "WebP source image is empty";
				return false;
			}
			const auto* encoded = reinterpret_cast<const std::uint8_t*>(bytes.data());
			if (!WebPGetInfo(encoded, bytes.size(), &width, &height))
			{
				error = "libwebp could not parse source image metadata";
				return false;
			}

			std::size_t byteCount = 0;
			if (!TryGetRgba8ByteCount(width, height, byteCount) || width > std::numeric_limits<int>::max() / 4)
			{
				error = "decoded WebP dimensions overflow RGBA8 storage";
				return false;
			}
			rgba.resize(byteCount);
			if (!WebPDecodeRGBAInto(
				encoded,
				bytes.size(),
				reinterpret_cast<std::uint8_t*>(rgba.data()),
				rgba.size(),
				width * 4))
			{
				error = "libwebp failed to decode source image to RGBA8";
				return false;
			}
			return true;
		}

		float SrgbToLinear(float value)
		{
			return value <= 0.04045f
				? value / 12.92f
				: std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSrgb(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			return value <= 0.0031308f
				? value * 12.92f
				: 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
		}

		std::byte FloatToByte(float value)
		{
			const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
			return static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(scaled)));
		}

		float ByteToFloat(std::byte value)
		{
			return static_cast<float>(std::to_integer<std::uint8_t>(value)) / 255.0f;
		}

		void DownsampleRgba8(
			std::span<const std::byte> source,
			std::uint32_t sourceWidth,
			std::uint32_t sourceHeight,
			std::uint32_t destinationWidth,
			std::uint32_t destinationHeight,
			Swim::Assets::TextureColorSpace colorSpace,
			Swim::Assets::TextureSemantic semantic,
			std::vector<std::byte>& destination)
		{
			destination.resize(static_cast<std::size_t>(destinationWidth) * destinationHeight * 4u);
			const bool srgbColor = colorSpace == Swim::Assets::TextureColorSpace::SRgb && semantic == Swim::Assets::TextureSemantic::Color;
			const bool normalMap = semantic == Swim::Assets::TextureSemantic::Normal;

			for (std::uint32_t y = 0; y < destinationHeight; ++y)
			{
				const std::uint32_t sourceYBegin = static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * sourceHeight) / destinationHeight);
				const std::uint32_t sourceYEnd = std::max(
					sourceYBegin + 1u,
					static_cast<std::uint32_t>((static_cast<std::uint64_t>(y + 1u) * sourceHeight) / destinationHeight));

				for (std::uint32_t x = 0; x < destinationWidth; ++x)
				{
					const std::uint32_t sourceXBegin = static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * sourceWidth) / destinationWidth);
					const std::uint32_t sourceXEnd = std::max(
						sourceXBegin + 1u,
						static_cast<std::uint32_t>((static_cast<std::uint64_t>(x + 1u) * sourceWidth) / destinationWidth));

					float accumulated[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
					std::uint32_t sampleCount = 0;
					for (std::uint32_t sourceY = sourceYBegin; sourceY < sourceYEnd; ++sourceY)
					{
						for (std::uint32_t sourceX = sourceXBegin; sourceX < sourceXEnd; ++sourceX)
						{
							const std::size_t sourceOffset = (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4u;
							for (std::size_t channel = 0; channel < 4; ++channel)
							{
								float value = ByteToFloat(source[sourceOffset + channel]);
								if (srgbColor && channel < 3)
								{
									value = SrgbToLinear(value);
								}
								else if (normalMap && channel < 3)
								{
									value = value * 2.0f - 1.0f;
								}
								accumulated[channel] += value;
							}
							++sampleCount;
						}
					}

					const float inverseSampleCount = 1.0f / static_cast<float>(sampleCount);
					for (float& value : accumulated)
					{
						value *= inverseSampleCount;
					}

					if (normalMap)
					{
						const float lengthSquared =
							accumulated[0] * accumulated[0] +
							accumulated[1] * accumulated[1] +
							accumulated[2] * accumulated[2];
						if (lengthSquared > 0.000001f)
						{
							const float inverseLength = 1.0f / std::sqrt(lengthSquared);
							accumulated[0] *= inverseLength;
							accumulated[1] *= inverseLength;
							accumulated[2] *= inverseLength;
						}
						else
						{
							accumulated[0] = 0.0f;
							accumulated[1] = 0.0f;
							accumulated[2] = 1.0f;
						}
					}

					const std::size_t destinationOffset = (static_cast<std::size_t>(y) * destinationWidth + x) * 4u;
					for (std::size_t channel = 0; channel < 4; ++channel)
					{
						float value = accumulated[channel];
						if (srgbColor && channel < 3)
						{
							value = LinearToSrgb(value);
						}
						else if (normalMap && channel < 3)
						{
							value = value * 0.5f + 0.5f;
						}
						destination[destinationOffset + channel] = FloatToByte(value);
					}
				}
			}
		}

		bool BuildMipChain(
			std::vector<std::byte> baseLevel,
			std::uint32_t width,
			std::uint32_t height,
			Swim::Assets::TextureColorSpace colorSpace,
			Swim::Assets::TextureSemantic semantic,
			Swim::Assets::TexturePayloadVariant& payload)
		{
			std::vector<std::byte> current = std::move(baseLevel);
			std::uint32_t currentWidth = width;
			std::uint32_t currentHeight = height;

			while (true)
			{
				if (payload.Bytes.size() > std::numeric_limits<std::uint64_t>::max() - current.size())
				{
					return false;
				}
				const std::uint64_t offset = static_cast<std::uint64_t>(payload.Bytes.size());
				payload.Mips.push_back({
					currentWidth,
					currentHeight,
					1,
					offset,
					static_cast<std::uint64_t>(current.size()),
					static_cast<std::uint64_t>(current.size())
				});
				payload.Bytes.insert(payload.Bytes.end(), current.begin(), current.end());

				if (currentWidth == 1 && currentHeight == 1)
				{
					break;
				}

				const std::uint32_t nextWidth = std::max(1u, currentWidth / 2u);
				const std::uint32_t nextHeight = std::max(1u, currentHeight / 2u);
				std::vector<std::byte> next;
				DownsampleRgba8(current, currentWidth, currentHeight, nextWidth, nextHeight, colorSpace, semantic, next);
				current = std::move(next);
				currentWidth = nextWidth;
				currentHeight = nextHeight;
			}
			return true;
		}
	}

	SourceImageMimeType DetectSourceImageMimeType(std::span<const std::byte> bytes, SourceImageMimeType hint)
	{
		if (hint != SourceImageMimeType::Unknown)
		{
			return hint;
		}

		static constexpr std::array<std::byte, 8> PngMagic
		{
			std::byte{ 0x89 }, std::byte{ 0x50 }, std::byte{ 0x4E }, std::byte{ 0x47 },
			std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
		};
		static constexpr std::array<std::byte, 3> JpegMagic
		{
			std::byte{ 0xFF }, std::byte{ 0xD8 }, std::byte{ 0xFF }
		};
		static constexpr std::array<std::byte, 12> Ktx2Magic
		{
			std::byte{ 0xAB }, std::byte{ 0x4B }, std::byte{ 0x54 }, std::byte{ 0x58 },
			std::byte{ 0x20 }, std::byte{ 0x32 }, std::byte{ 0x30 }, std::byte{ 0xBB },
			std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A }
		};

		if (HasPrefix(bytes, PngMagic))
		{
			return SourceImageMimeType::Png;
		}
		if (HasPrefix(bytes, JpegMagic))
		{
			return SourceImageMimeType::Jpeg;
		}
		if (HasPrefix(bytes, Ktx2Magic))
		{
			return SourceImageMimeType::Ktx2;
		}
		if (bytes.size() >= 12 &&
			std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
			std::memcmp(bytes.data() + 8, "WEBP", 4) == 0)
		{
			return SourceImageMimeType::WebP;
		}
		if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0)
		{
			return SourceImageMimeType::Dds;
		}
		return SourceImageMimeType::Unknown;
	}

	SourceImageTextureCompileResult CompileSourceImageTexture(
		std::span<const std::byte> bytes,
		SourceImageMimeType mimeType,
		Swim::Assets::TextureColorSpace colorSpace,
		Swim::Assets::TextureSemantic semantic)
	{
		mimeType = DetectSourceImageMimeType(bytes, mimeType);
		if (mimeType != SourceImageMimeType::Png && mimeType != SourceImageMimeType::Jpeg && mimeType != SourceImageMimeType::WebP)
		{
			return MakeError(SourceImageTextureCompileErrorCode::UnsupportedSource, "source texture is not PNG, JPEG, or WebP");
		}

		std::vector<std::byte> rgba;
		int width = 0;
		int height = 0;
		std::string decodeError;
		const bool decoded = mimeType == SourceImageMimeType::WebP
			? DecodeWebPRgba8(bytes, rgba, width, height, decodeError)
			: DecodeStbRgba8(bytes, rgba, width, height, decodeError);
		if (!decoded)
		{
			return MakeError(SourceImageTextureCompileErrorCode::InvalidData, std::move(decodeError));
		}
		if (static_cast<std::uint64_t>(width) > std::numeric_limits<std::uint32_t>::max() ||
			static_cast<std::uint64_t>(height) > std::numeric_limits<std::uint32_t>::max())
		{
			return MakeError(SourceImageTextureCompileErrorCode::Overflow, "decoded source image dimensions exceed TextureAsset limits");
		}

		SourceImageTextureCompileResult result;
		result.Asset.Dimension = Swim::Assets::TextureDimension::Texture2D;
		result.Asset.ColorSpace = colorSpace;
		result.Asset.Semantic = semantic;
		result.Asset.Width = static_cast<std::uint32_t>(width);
		result.Asset.Height = static_cast<std::uint32_t>(height);
		result.Asset.Depth = 1;
		result.Asset.ArrayLayers = 1;

		Swim::Assets::TexturePayloadVariant payload;
		payload.Container = Swim::Assets::TextureContainerFormat::NativeMipData;
		payload.Format = colorSpace == Swim::Assets::TextureColorSpace::SRgb
			? Swim::Assets::TexturePayloadFormat::RGBA8SRgb
			: Swim::Assets::TexturePayloadFormat::RGBA8UNorm;
		payload.Supercompression = Swim::Assets::TextureSupercompression::None;
		if (!BuildMipChain(std::move(rgba), result.Asset.Width, result.Asset.Height, colorSpace, semantic, payload))
		{
			return MakeError(SourceImageTextureCompileErrorCode::Overflow, "generated source-image mip chain exceeds TextureAsset payload limits");
		}
		result.Asset.Payloads.push_back(std::move(payload));
		return result;
	}

}
