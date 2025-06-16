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

		// This will always load them from the same directory as the executable and from Assets/Textures.
		// TO CONSIDER: we won't want to load every asset right away always especially once games made with this engine get large, we only want to load the active scenes textures.
		void LoadAllRecursively();

		// Caches in textureCount field, which you can get with GetTextureCount()
		void FetchTextureCount();

		const unsigned int GetTextureCount() const { return textureCount; }

		// Loads a texture if not already loaded, returns shared_ptr
		std::shared_ptr<Texture2D> LoadTexture(const std::string& fileName);

		void StoreTextureManually(const std::shared_ptr<Texture2D>& texture, const std::string& name);

		std::shared_ptr<Texture2D> GetTexture2D(const std::string& name);
		std::shared_ptr<Texture2D> GetTexture2DLazy(const std::string& name);
		std::string FormatKey(const std::string& filePath, const std::string& rootPath) const;

		// Will call FreeCPU on all textures in the map that don't contain any of the strings in keep
		void CleanCPU(const std::vector<std::string>& keep = {});

		// Frees everything
		void Flush();

		// Get a fixed size array of textures that have a certain string in their name.
		// For example if you want to get exactly 10 textures with the name "sword" in it.
		// This is a fixed size since most internal engine functions use fixed arrays of data, such as cubemap face lists.
		// If needed we can implement a vector returning version of this.
		// This also returns the textures sorted by name based on trailing digits (if any) from least to greatest.
		// For example: cubemap0, cubemap1, cubemap2, etc
		template <size_t N>
		std::array<std::shared_ptr<Texture2D>, N> GetTexturesContainingString(const std::string& substring)
		{
			std::vector<std::pair<int, std::shared_ptr<Texture2D>>> sortedTextures;

			{ // lock this part of execution
				std::lock_guard<std::mutex> lock(poolMutex);

				for (const auto& [key, texture] : textures)
				{
					if (key.find(substring) != std::string::npos)
					{
						int index = ExtractTrailingNumber(key);

						if (index >= 0)
						{
							sortedTextures.emplace_back(index, texture);
						}
					}
				}
			}

			std::sort(sortedTextures.begin(), sortedTextures.end(),
				[](const auto& a, const auto& b)
			{
				return a.first < b.first;
			});

			std::array<std::shared_ptr<Texture2D>, N> result{};

			for (size_t i = 0; i < std::min(N, sortedTextures.size()); ++i)
			{
				result[i] = sortedTextures[i].second;
			}

			return result;
		}

	private:

		// Private constructor for Singleton pattern
		TexturePool() = default;

		std::mutex poolMutex;
		std::unordered_map<std::string, std::shared_ptr<Texture2D>> textures;

		unsigned int textureCount{ 0 };

		int ExtractTrailingNumber(const std::string& str)
		{
			// Start from the end and go backwards until we hit a non-digit
			auto it = str.rbegin();
			std::string numberStr;

			while (it != str.rend() && std::isdigit(*it))
			{
				numberStr.insert(numberStr.begin(), *it);
				++it;
			}

			if (!numberStr.empty())
			{
				try
				{
					return std::stoi(numberStr);
				}
				catch (...)
				{
					// overflow or invalid, ignore
				}
			}

			// Return -1 to indicate no trailing number (or use INT_MAX to sort last)
			return -1;
		}

	};

}
