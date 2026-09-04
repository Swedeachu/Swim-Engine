#pragma once

#include "Texture2D.h"
#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/AssetId.h"
#include "Engine/Assets/ContentHash.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Swim::Assets
{
	class AssetSystem;
	struct TextureAsset;
}

namespace Swim::Platform
{
	class FileSystem;
}

namespace Engine
{

	class TexturePool
	{

	public:

		TexturePool(Swim::Platform::FileSystem& files, TextureRuntimeContext context);

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
		std::shared_ptr<Texture2D> LoadTexture(const std::string& fileName, bool generateMips);

		// Transitional renderer residency adapter for authoritative runtime texture assets.
		std::shared_ptr<Texture2D> GetOrCreateTextureFromAsset(
			Swim::Assets::AssetSystem& assets,
			Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> handle,
			const std::string& debugName
		);

		// Explicit compatibility residency request. Texture2D construction is CPU-only.
		void RequestTextureResidency(const std::shared_ptr<Texture2D>& texture);

		void StoreTextureManually(const std::shared_ptr<Texture2D>& texture, const std::string& name);

		std::shared_ptr<Texture2D> CreateTransientTexture(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name = "<generated>", bool generateMips = true);

		int GetLiveTextureCount() const
		{
			return lifetimeTracker ? lifetimeTracker->LiveCount.load(std::memory_order_relaxed) : 0;
		}

		const TextureRuntimeContext& GetRuntimeContext() const { return runtimeContext; }

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

		struct AssetTextureKey
		{
			Swim::Assets::AssetId Id{};
			std::uint32_t Generation = 0;
			Swim::Assets::ContentHash Hash{};

			bool operator==(const AssetTextureKey&) const = default;
		};

		struct AssetTextureKeyHash
		{
			std::size_t operator()(const AssetTextureKey& key) const noexcept
			{
				std::size_t value = std::hash<std::uint64_t>{}(key.Id.Value);
				value ^= std::hash<std::uint32_t>{}(key.Generation) << 1;
				value ^= std::hash<Swim::Assets::ContentHash>{}(key.Hash) << 2;
				return value;
			}
		};

		Swim::Platform::FileSystem* files = nullptr;
		TextureRuntimeContext runtimeContext{};
		std::shared_ptr<TextureLifetimeTracker> lifetimeTracker;

		std::mutex poolMutex;
		std::unordered_map<std::string, std::shared_ptr<Texture2D>> textures;
		std::unordered_map<Swim::Assets::ContentHash, std::weak_ptr<Texture2D>> textureContentIndex;
		std::unordered_map<AssetTextureKey, std::weak_ptr<Texture2D>, AssetTextureKeyHash> assetTextureIndex;

		unsigned int textureCount{ 0 };

		void RequestTextureResidencyLocked(const std::shared_ptr<Texture2D>& texture);

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
