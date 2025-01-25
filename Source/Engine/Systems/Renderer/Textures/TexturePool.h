#pragma once

#include "Texture2D.h"
#include <mutex>
#include <unordered_map>

namespace Engine
{

	class TexturePool
	{

	public:

		static TexturePool& GetInstance();

		// Delete copy and move constructors
		TexturePool(const TexturePool&) = delete;
		TexturePool& operator=(const TexturePool&) = delete;
		TexturePool(TexturePool&&) = delete;
		TexturePool& operator=(TexturePool&&) = delete;

		// This will always load them from the same directory as the executable and from Assets/Textures
		void LoadAllRecursively();

		// Loads a texture if not already loaded, returns shared_ptr
		std::shared_ptr<Texture2D> LoadTexture(const std::string& fileName);

		std::shared_ptr<Texture2D> GetTexture2D(const std::string& name);
		std::shared_ptr<Texture2D> GetTexture2DLazy(const std::string& name);
		std::string FormatKey(const std::string& filePath, const std::string& rootPath) const;

		// Frees everything
		void Flush();

	private:

		// Private constructor for Singleton pattern
		TexturePool() = default;

		std::mutex poolMutex;
		std::unordered_map<std::string, std::shared_ptr<Texture2D>> textures;

	};

}
