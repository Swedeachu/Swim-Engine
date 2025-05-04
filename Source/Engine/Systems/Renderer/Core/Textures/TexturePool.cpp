#include "PCH.h"
#include "TexturePool.h"
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace Engine
{

	TexturePool& TexturePool::GetInstance()
	{
		static TexturePool instance;
		return instance;
	}

	void TexturePool::LoadAllRecursively()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		const std::string textureRoot = "Assets\\Textures";
		for (auto& p : std::filesystem::recursive_directory_iterator(textureRoot))
		{
			if (p.is_regular_file())
			{
				auto ext = p.path().extension().string();
				if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") // Supported image formats
				{
					std::string fullPath = p.path().string();

					// Create the formatted key
					std::string key = FormatKey(fullPath, textureRoot);

					if (textures.find(key) == textures.end())
					{
						textures[key] = std::make_shared<Texture2D>(fullPath);
					}
				}
			}
		}
	}

	// scuffed copy and paste job to call before LoadAllRecursively() so we can get an idea of how much space to allocate in our bindless texture array
	void TexturePool::FetchTextureCount()
	{
		const std::string textureRoot = "Assets\\Textures";
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

	std::shared_ptr<Texture2D> TexturePool::LoadTexture(const std::string& fileName)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		std::string key = FormatKey(fileName, fileName);

		auto it = textures.find(key);
		if (it != textures.end())
		{
			// Already loaded
			return it->second;
		}

		// Not found, load now
		auto tex = std::make_shared<Texture2D>(fileName);
		textures[key] = tex;
		return tex;
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

	void TexturePool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& texture : textures)
		{
			texture.second.get()->Free(); 
		}

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
