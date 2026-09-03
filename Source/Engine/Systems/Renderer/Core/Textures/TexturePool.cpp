#include "PCH.h"
#include "TexturePool.h"
#include "Engine/Platform/FileSystem.h"
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace Engine
{

	TexturePool::TexturePool(Swim::Platform::FileSystem& files, TextureRuntimeContext context)
		: files(&files), runtimeContext(std::move(context))
	{
		if (!runtimeContext.Lifetime)
		{
			runtimeContext.Lifetime = std::make_shared<TextureLifetimeTracker>();
		}
		lifetimeTracker = runtimeContext.Lifetime;
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
						textures[key] = std::make_shared<Texture2D>(runtimeContext, fullPath);
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
		auto tex = std::make_shared<Texture2D>(runtimeContext, fileName, generateMips);
		textures[key] = tex;
		return tex;
	}

	std::shared_ptr<Texture2D> TexturePool::GetOrCreateTextureFromTinyGltfImage(const tinygltf::Image& image, const std::string& imageKey)
	{
		// Deduplication: search through all textures for an identical one
		for (const auto& [existingName, existingTex] : textures)
		{
			if (!existingTex)
			{
				continue;
			}

			// Match width, height, and pixel content
			if (image.width == static_cast<int>(existingTex->GetWidth()) &&
				image.height == static_cast<int>(existingTex->GetHeight()) &&
				image.image.size() == existingTex->GetDataSize() &&
				std::memcmp(image.image.data(), existingTex->GetData(), image.image.size()) == 0)
			{
				// Found duplicate: reuse it
				// textures[imageKey] = existingTex; // not sure if making a new record for something already in the pool is a good idea, probably not so don't do it
				// std::cout << "[INFO] Reusing existing texture \"" << existingName << "\" for new image \"" << imageKey << "\"\n";
				return existingTex;
			}
		}

		// If no identical texture found, create a new one
		std::shared_ptr<Texture2D> texture = std::make_shared<Texture2D>(
			runtimeContext,
			image.width,
			image.height,
			image.image.data(),
			imageKey // StoreTextureManually() could end up making the key in the texture pool map be something different, doesn't matter much for now
		);

		texture->isPixelDataSTB = false;

		// Store texture even if the name is reused — StoreTextureManually handles that
		this->StoreTextureManually(texture, imageKey);

		return texture;
	}

	std::shared_ptr<Texture2D> TexturePool::CreateTextureFromTinyGltfImage(const tinygltf::Image& image, const std::string& debugName)
	{
		// Validate image dimensions and data
		if (image.width <= 0 || image.height <= 0 || image.image.empty())
		{
			std::cerr << "[TexturePool] Invalid image: " << debugName << " (width/height or data missing)\n";
			return nullptr;
		}

		// Only support 4-channel RGBA 8-bit textures (as expected by your KTX2 loader)
		if (image.component != 4 || image.bits != 8)
		{
			std::cerr << "[TexturePool] Unsupported image format in: " << debugName
				<< " (components: " << image.component << ", bits: " << image.bits << ")\n";
			return nullptr;
		}

		// Upload texture to GPU (or staging structure)
		std::shared_ptr<Texture2D> texture = std::make_shared<Texture2D>(
			runtimeContext,
			image.width,
			image.height,
			image.image.data(),
			debugName
		);

		// Tag if the data came from STB or not 
		texture->isPixelDataSTB = false;

		// Store it by name to reuse later
		this->StoreTextureManually(texture, debugName);

		return texture;
	}

	std::shared_ptr<Texture2D> TexturePool::CreateTransientTexture(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name, bool generateMips)
	{
		return std::make_shared<Texture2D>(runtimeContext, width, height, rgbaData, name, generateMips);
	}

	void TexturePool::StoreTextureManually(const std::shared_ptr<Texture2D>& texture, const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		std::string finalName = name;
		int counter = 1;

		// Incrementally search for a free name, we have to do this a lot because GLB textures are often nameless and duplicated/messed up in the binary for the name field
		while (textures.find(finalName) != textures.end())
		{
			finalName = name + "_" + std::to_string(counter);
			counter++;
		}

		// Temp debug code to just tell us if this ever happens
	#ifdef _DEBUG 
		constexpr bool run = false; // just quick flag if we even want to bother running this code while in debug mode
		if constexpr (run) 
		{
			for (const auto& [existingName, existingTex] : textures)
			{
				if (existingTex && *existingTex == *texture)
				{
					std::cout << "[INFO] \"" << name << "\" is identical to already-stored \"" << existingName << "\"\n";
				}
			}

			if (finalName != name)
			{
				std::cout << "Texture with name \"" << name << "\" already exists in the texture pool!\n";
				std::cout << "Renaming to \"" << finalName << "\"\n";
			}
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
