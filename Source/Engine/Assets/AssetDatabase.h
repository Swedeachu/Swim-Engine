#pragma once

#include "Engine/Assets/AssetId.h"

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Swim::Assets
{

	struct AssetPathEntry
	{
		AssetId Id{};
		std::string LogicalPath;
	};

	std::string NormalizeAssetPath(std::string_view path);

	class AssetDatabase
	{
	public:

		AssetId GetOrCreate(std::string_view path);
		bool Bind(AssetId id, std::string_view path);
		bool Rebind(AssetId id, std::string_view newPath);
		bool RemovePath(std::string_view path);

		std::optional<AssetId> FindId(std::string_view path) const;
		std::optional<std::string> FindPath(AssetId id) const;
		std::vector<AssetPathEntry> Snapshot() const;

		std::size_t GetCount() const;
		void Clear();

	private:

		AssetId MakeIdForPathLocked(const std::string& normalizedPath) const;

		mutable std::shared_mutex mutex;
		std::unordered_map<std::string, AssetId> pathToId;
		std::unordered_map<AssetId, std::string> idToPath;
	};

}
