#include "PCH.h"
#include "TexturePool.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Platform/FileSystem.h"

#include <basisu_transcoder.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <sstream>
#include <stdexcept>

namespace Engine
{

	namespace
	{
		Swim::Assets::ContentHash ComputeLegacyTextureContentHash(std::uint32_t width, std::uint32_t height, std::span<const std::byte> bytes)
		{
			const Swim::Assets::ContentHash payloadHash = Swim::Assets::ComputeContentHash(bytes);
			std::array<std::byte, 40> combined{};
			std::memcpy(combined.data(), &width, sizeof(width));
			std::memcpy(combined.data() + 4, &height, sizeof(height));
			std::memcpy(combined.data() + 8, payloadHash.Bytes.data(), payloadHash.Bytes.size());
			return Swim::Assets::ComputeContentHash(combined);
		}

		bool IsRgba8(Swim::Assets::TexturePayloadFormat format)
		{
			return format == Swim::Assets::TexturePayloadFormat::RGBA8UNorm || format == Swim::Assets::TexturePayloadFormat::RGBA8SRgb;
		}

		bool TryGetRgba8ByteCount(std::uint32_t width, std::uint32_t height, std::uint64_t& size)
		{
			if (width == 0 || height == 0)
			{
				return false;
			}
			const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
			if (pixelCount > std::numeric_limits<std::uint64_t>::max() / 4u)
			{
				return false;
			}
			size = pixelCount * 4u;
			return size <= std::numeric_limits<std::size_t>::max();
		}

		bool CopyRawRgbaMip(
			const Swim::Assets::TexturePayloadVariant& payload,
			std::vector<unsigned char>& rgba)
		{
			if (!IsRgba8(payload.Format) || payload.Mips.empty())
			{
				return false;
			}
			const Swim::Assets::TextureMipDesc& mip = payload.Mips.front();
			std::uint64_t expectedSize = 0;
			if (!TryGetRgba8ByteCount(mip.Width, mip.Height, expectedSize) || mip.SizeBytes != expectedSize ||
				mip.OffsetBytes > payload.Bytes.size() || mip.SizeBytes > payload.Bytes.size() - mip.OffsetBytes)
			{
				return false;
			}
			const std::byte* source = payload.Bytes.data() + mip.OffsetBytes;
			rgba.resize(static_cast<std::size_t>(mip.SizeBytes));
			std::memcpy(rgba.data(), source, rgba.size());
			return true;
		}

		bool DecompressZstdRgbaMip(
			const Swim::Assets::TexturePayloadVariant& payload,
			std::vector<unsigned char>& rgba)
		{
			if (!IsRgba8(payload.Format) || payload.Mips.empty())
			{
				return false;
			}
			const Swim::Assets::TextureMipDesc& mip = payload.Mips.front();
			std::uint64_t expectedSize = 0;
			if (!TryGetRgba8ByteCount(mip.Width, mip.Height, expectedSize) || mip.UncompressedSizeBytes != expectedSize ||
				mip.OffsetBytes > payload.Bytes.size() || mip.SizeBytes > payload.Bytes.size() - mip.OffsetBytes)
			{
				return false;
			}
			rgba.resize(static_cast<std::size_t>(expectedSize));
			const std::size_t result = ZSTD_decompress(
				rgba.data(),
				rgba.size(),
				payload.Bytes.data() + mip.OffsetBytes,
				static_cast<std::size_t>(mip.SizeBytes)
			);
			return !ZSTD_isError(result) && result == rgba.size();
		}

		bool TranscodeBasisKtx2(
			const Swim::Assets::TexturePayloadVariant& payload,
			std::vector<unsigned char>& rgba,
			std::uint32_t& width,
			std::uint32_t& height)
		{
			if (payload.Container != Swim::Assets::TextureContainerFormat::Ktx2 || payload.Bytes.empty())
			{
				return false;
			}

			static std::once_flag initializeTranscoder;
			std::call_once(initializeTranscoder, []()
			{
				basist::basisu_transcoder_init();
			});

			if (payload.Bytes.size() > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}

			basist::ktx2_transcoder transcoder;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.Bytes.data());
			if (!transcoder.init(bytes, static_cast<std::uint32_t>(payload.Bytes.size())) || !transcoder.start_transcoding())
			{
				return false;
			}

			width = transcoder.get_width();
			height = transcoder.get_height();
			if (width == 0 || height == 0)
			{
				return false;
			}
			const std::uint64_t pixelCount64 = static_cast<std::uint64_t>(width) * height;
			if (pixelCount64 > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}
			const std::uint32_t pixelCount = static_cast<std::uint32_t>(pixelCount64);
			rgba.resize(static_cast<std::size_t>(pixelCount) * 4u);
			return transcoder.transcode_image_level(
				0,
				0,
				0,
				rgba.data(),
				pixelCount,
				basist::transcoder_texture_format::cTFRGBA32
			);
		}

		bool DecodeTextureAsset(
			const Swim::Assets::TextureAsset& asset,
			std::vector<unsigned char>& rgba,
			std::uint32_t& width,
			std::uint32_t& height)
		{
			if (asset.Dimension != Swim::Assets::TextureDimension::Texture2D || asset.ArrayLayers != 1 || asset.Depth != 1)
			{
				return false;
			}

			width = asset.Width;
			height = asset.Height;
			for (const Swim::Assets::TexturePayloadVariant& payload : asset.Payloads)
			{
				if (!payload.Mips.empty() && payload.Mips.front().Width == asset.Width && payload.Mips.front().Height == asset.Height)
				{
					if (payload.Supercompression == Swim::Assets::TextureSupercompression::None && CopyRawRgbaMip(payload, rgba))
					{
						return true;
					}
					if (payload.Supercompression == Swim::Assets::TextureSupercompression::Zstandard && DecompressZstdRgbaMip(payload, rgba))
					{
						return true;
					}
				}
				std::uint32_t transcodedWidth = 0;
				std::uint32_t transcodedHeight = 0;
				if (TranscodeBasisKtx2(payload, rgba, transcodedWidth, transcodedHeight) &&
					transcodedWidth == asset.Width && transcodedHeight == asset.Height)
				{
					width = transcodedWidth;
					height = transcodedHeight;
					return true;
				}
			}
			return false;
		}
	}

	TexturePool::TexturePool(Swim::Platform::FileSystem& files, TextureRuntimeContext context)
		: files(&files), runtimeContext(std::move(context))
	{
		if (!runtimeContext.Lifetime)
		{
			runtimeContext.Lifetime = std::make_shared<TextureLifetimeTracker>();
		}
		lifetimeTracker = runtimeContext.Lifetime;
	}

	void TexturePool::RequestTextureResidency(const std::shared_ptr<Texture2D>& texture)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		RequestTextureResidencyLocked(texture);
	}

	void TexturePool::RequestTextureResidencyLocked(const std::shared_ptr<Texture2D>& texture)
	{
		if (!texture)
		{
			throw std::runtime_error("Cannot request renderer residency for a null Texture2D.");
		}

		texture->MakeResident(runtimeContext);
	}

	void TexturePool::LoadAllRecursively()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		const std::filesystem::path textureRoot = files->ResolveAssetPath("Textures");
		for (auto& p : std::filesystem::recursive_directory_iterator(textureRoot))
		{
			if (p.is_regular_file())
			{
				auto ext = p.path().extension().string();
				if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") // Supported image formats
				{
					std::string fullPath = p.path().string();

					// Create the formatted key
					std::string key = FormatKey(fullPath, textureRoot.string());

					if (textures.find(key) == textures.end())
					{
						auto texture = std::make_shared<Texture2D>(fullPath);
						RequestTextureResidencyLocked(texture);
						textures[key] = std::move(texture);
					}
				}
			}
		}

		// Free all images on the CPU side of things that are not a cubemap since we need cubemap textures for cpu side image processing
		CleanCPU({ "Cubemap" });
	}

	// scuffed copy and paste job to call before LoadAllRecursively() so we can get an idea of how much space to allocate in our bindless texture array
	void TexturePool::FetchTextureCount()
	{
		const std::filesystem::path textureRoot = files->ResolveAssetPath("Textures");
		for (auto& p : std::filesystem::recursive_directory_iterator(textureRoot))
		{
			if (p.is_regular_file())
			{
				auto ext = p.path().extension().string();
				if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") // Supported image formats
				{
					textureCount++;
				}
			}
		}
	}

	std::shared_ptr<Texture2D> TexturePool::LoadTexture(const std::string& fileName, bool generateMips)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		std::string parent = std::filesystem::path(fileName).parent_path().string();
		std::string key = FormatKey(fileName, parent);

		auto it = textures.find(key);
		if (it != textures.end())
		{
			// Already loaded
			return it->second;
		}

		// Not found, load now
		auto tex = std::make_shared<Texture2D>(fileName, generateMips);
		RequestTextureResidencyLocked(tex);
		textures[key] = tex;
		return tex;
	}

	std::shared_ptr<Texture2D> TexturePool::GetOrCreateTextureFromAsset(
		Swim::Assets::AssetSystem& assets,
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> handle,
		const std::string& debugName)
	{
		if (!handle)
		{
			return nullptr;
		}

		const Swim::Assets::AssetStatus status = assets.GetStatus(handle);
		AssetTextureKey assetKey{ handle.GetId(), handle.GetGeneration(), status.Hash };
		if (!assetKey.Hash.IsZero())
		{
			std::lock_guard<std::mutex> lock(poolMutex);
			auto existing = assetTextureIndex.find(assetKey);
			if (existing != assetTextureIndex.end())
			{
				if (std::shared_ptr<Texture2D> texture = existing->second.lock())
				{
					return texture;
				}
				assetTextureIndex.erase(existing);
			}
		}

		const Swim::Assets::TextureAsset* asset = assets.Resolve(handle);
		if (!asset)
		{
			throw std::runtime_error("Cannot create renderer texture residency for a non-resident TextureAsset: " + debugName);
		}

		std::vector<unsigned char> rgba;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		if (!DecodeTextureAsset(*asset, rgba, width, height) || rgba.empty())
		{
			throw std::runtime_error("Legacy renderer cannot decode the cooked texture payload: " + debugName);
		}

		const auto rgbaBytes = std::as_bytes(std::span(rgba.data(), rgba.size()));
		const Swim::Assets::ContentHash contentHash = ComputeLegacyTextureContentHash(width, height, rgbaBytes);
		if (assetKey.Hash.IsZero())
		{
			assetKey.Hash = contentHash;
		}

		std::lock_guard<std::mutex> lock(poolMutex);
		auto existingAsset = assetTextureIndex.find(assetKey);
		if (existingAsset != assetTextureIndex.end())
		{
			if (std::shared_ptr<Texture2D> texture = existingAsset->second.lock())
			{
				return texture;
			}
			assetTextureIndex.erase(existingAsset);
		}

		auto content = textureContentIndex.find(contentHash);
		if (content != textureContentIndex.end())
		{
			if (std::shared_ptr<Texture2D> texture = content->second.lock())
			{
				assetTextureIndex[assetKey] = texture;
				return texture;
			}
			textureContentIndex.erase(content);
		}

		auto texture = std::make_shared<Texture2D>(width, height, rgba.data(), debugName, true);
		RequestTextureResidencyLocked(texture);
		texture->isPixelDataSTB = false;
		textureContentIndex[contentHash] = texture;
		assetTextureIndex[assetKey] = texture;

		std::string key = debugName;
		int counter = 1;
		while (textures.find(key) != textures.end())
		{
			key = debugName + "_" + std::to_string(counter++);
		}
		textures.emplace(std::move(key), texture);
		return texture;
	}

	std::shared_ptr<Texture2D> TexturePool::CreateTransientTexture(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name, bool generateMips)
	{
		auto texture = std::make_shared<Texture2D>(width, height, rgbaData, name, generateMips);
		RequestTextureResidency(texture);
		return texture;
	}

	void TexturePool::StoreTextureManually(const std::shared_ptr<Texture2D>& texture, const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		RequestTextureResidencyLocked(texture);

		std::string finalName = name;
		int counter = 1;

		while (textures.find(finalName) != textures.end())
		{
			finalName = name + "_" + std::to_string(counter);
			counter++;
		}

	#ifdef _DEBUG
		if (finalName != name)
		{
			std::cout << "Texture with name \"" << name << "\" already exists in the texture pool!\n";
			std::cout << "Renaming to \"" << finalName << "\"\n";
		}
	#endif // _DEBUG

		textures[finalName] = texture;
	}

	std::shared_ptr<Texture2D> TexturePool::GetTexture2D(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = textures.find(name);
		if (it != textures.end())
		{
			return it->second;
		}

		throw std::runtime_error("Texture not found: " + name);
	}

	// Instead of specifying a full path, you can just short hand it
	// For example, instead of "Mart/mart" you can just pass "mart"
	std::shared_ptr<Texture2D> TexturePool::GetTexture2DLazy(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// Try to find a key that contains the given name
		for (const auto& [key, tex] : textures)
		{
			if (key.find(name) != std::string::npos)
			{
				return tex;
			}
		}

		throw std::runtime_error("Texture not found for lazy lookup: " + name);
	}

	void TexturePool::CleanCPU(const std::vector<std::string>& keep)
	{
		for (auto& texture : textures)
		{
			Texture2D* data = texture.second.get();
			const std::string& fp = data->GetFilePath();

			bool shouldKeep = false;

			for (const auto& str : keep)
			{
				if (fp.find(str) != std::string::npos)
				{
					shouldKeep = true;
					break;
				}
			}

			if (!shouldKeep)
			{
				data->FreeCPU();
			}
		}
	}

	void TexturePool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		// Will cause Texture2D destructor which calls Free() on the texture for us
		textures.clear();
		textureContentIndex.clear();
		assetTextureIndex.clear();
	}

	std::string TexturePool::FormatKey(const std::string& filePath, const std::string& rootPath) const
	{
		// Remove the root path if present
		std::string key = filePath;
		if (key.find(rootPath) == 0)
		{
			key = key.substr(rootPath.size() + 1); // +1 to remove the trailing slash
		}

		// Remove the file extension
		auto lastDot = key.find_last_of('.');
		if (lastDot != std::string::npos)
		{
			key = key.substr(0, lastDot);
		}

		// Convert to consistent path separator
		std::replace(key.begin(), key.end(), '\\', '/');
		return key;
	}

}
