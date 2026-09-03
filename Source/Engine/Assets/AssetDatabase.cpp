#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/ContentHash.h"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <stdexcept>

namespace Swim::Assets
{

	namespace
	{

		AssetId AssetIdFromHash(const ContentHash& hash)
		{
			std::uint64_t value = 0;
			for (std::size_t index = 0; index < 8; ++index)
			{
				value = (value << 8) | hash.Bytes[index];
			}
			if (value == 0)
			{
				value = 1;
			}
			return AssetId{ value };
		}

	}

	std::string NormalizeAssetPath(std::string_view path)
	{
		if (path.empty())
		{
			throw std::invalid_argument("Asset path cannot be empty.");
		}

		std::string input(path);
		std::replace(input.begin(), input.end(), '\\', '/');
		if (input.front() == '/' || input.find(':') != std::string::npos)
		{
			throw std::invalid_argument("Asset paths must be logical relative paths.");
		}

		std::vector<std::string> segments;
		std::size_t start = 0;
		while (start <= input.size())
		{
			const std::size_t end = input.find('/', start);
			const std::size_t count = end == std::string::npos ? input.size() - start : end - start;
			const std::string segment = input.substr(start, count);

			if (segment.empty() || segment == ".")
			{
				// Repeated separators and explicit current-directory segments are canonicalized away.
			}
			else if (segment == "..")
			{
				if (segments.empty())
				{
					throw std::invalid_argument("Asset path cannot escape the logical asset root.");
				}
				segments.pop_back();
			}
			else
			{
				if (segment.find('\0') != std::string::npos)
				{
					throw std::invalid_argument("Asset path contains an embedded null character.");
				}
				segments.push_back(segment);
			}

			if (end == std::string::npos)
			{
				break;
			}
			start = end + 1;
		}

		if (segments.empty())
		{
			throw std::invalid_argument("Asset path resolves to the logical asset root.");
		}

		std::string normalized;
		for (std::size_t index = 0; index < segments.size(); ++index)
		{
			if (index != 0)
			{
				normalized.push_back('/');
			}
			normalized += segments[index];
		}
		return normalized;
	}

	AssetId AssetDatabase::GetOrCreate(std::string_view path)
	{
		const std::string normalized = NormalizeAssetPath(path);
		std::unique_lock lock(mutex);

		const auto existing = pathToId.find(normalized);
		if (existing != pathToId.end())
		{
			return existing->second;
		}

		const AssetId id = MakeIdForPathLocked(normalized);
		pathToId.emplace(normalized, id);
		idToPath.emplace(id, normalized);
		return id;
	}

	bool AssetDatabase::Bind(AssetId id, std::string_view path)
	{
		if (!id.IsValid())
		{
			return false;
		}

		const std::string normalized = NormalizeAssetPath(path);
		std::unique_lock lock(mutex);

		const auto pathIt = pathToId.find(normalized);
		if (pathIt != pathToId.end() && pathIt->second != id)
		{
			return false;
		}

		const auto idIt = idToPath.find(id);
		if (idIt != idToPath.end() && idIt->second != normalized)
		{
			return false;
		}

		pathToId[normalized] = id;
		idToPath[id] = normalized;
		return true;
	}

	bool AssetDatabase::Rebind(AssetId id, std::string_view newPath)
	{
		if (!id.IsValid())
		{
			return false;
		}

		const std::string normalized = NormalizeAssetPath(newPath);
		std::unique_lock lock(mutex);

		const auto conflict = pathToId.find(normalized);
		if (conflict != pathToId.end() && conflict->second != id)
		{
			return false;
		}

		const auto current = idToPath.find(id);
		if (current != idToPath.end())
		{
			pathToId.erase(current->second);
		}

		pathToId[normalized] = id;
		idToPath[id] = normalized;
		return true;
	}

	bool AssetDatabase::RemovePath(std::string_view path)
	{
		const std::string normalized = NormalizeAssetPath(path);
		std::unique_lock lock(mutex);
		const auto existing = pathToId.find(normalized);
		if (existing == pathToId.end())
		{
			return false;
		}

		idToPath.erase(existing->second);
		pathToId.erase(existing);
		return true;
	}

	std::optional<AssetId> AssetDatabase::FindId(std::string_view path) const
	{
		const std::string normalized = NormalizeAssetPath(path);
		std::shared_lock lock(mutex);
		const auto existing = pathToId.find(normalized);
		if (existing == pathToId.end())
		{
			return std::nullopt;
		}
		return existing->second;
	}

	std::optional<std::string> AssetDatabase::FindPath(AssetId id) const
	{
		std::shared_lock lock(mutex);
		const auto existing = idToPath.find(id);
		if (existing == idToPath.end())
		{
			return std::nullopt;
		}
		return existing->second;
	}

	std::vector<AssetPathEntry> AssetDatabase::Snapshot() const
	{
		std::shared_lock lock(mutex);
		std::vector<AssetPathEntry> result;
		result.reserve(pathToId.size());
		for (const auto& [path, id] : pathToId)
		{
			result.push_back(AssetPathEntry{ id, path });
		}
		std::sort(result.begin(), result.end(), [](const AssetPathEntry& left, const AssetPathEntry& right)
		{
			return left.LogicalPath < right.LogicalPath;
		});
		return result;
	}

	std::size_t AssetDatabase::GetCount() const
	{
		std::shared_lock lock(mutex);
		return pathToId.size();
	}

	void AssetDatabase::Clear()
	{
		std::unique_lock lock(mutex);
		pathToId.clear();
		idToPath.clear();
	}

	AssetId AssetDatabase::MakeIdForPathLocked(const std::string& normalizedPath) const
	{
		AssetId candidate = AssetIdFromHash(ComputeContentHash(normalizedPath));
		std::uint32_t collisionIndex = 0;

		while (true)
		{
			const auto collision = idToPath.find(candidate);
			if (collision == idToPath.end() || collision->second == normalizedPath)
			{
				return candidate;
			}

			++collisionIndex;
			const std::string salted = normalizedPath + "#" + std::to_string(collisionIndex);
			candidate = AssetIdFromHash(ComputeContentHash(salted));
		}
	}

}
