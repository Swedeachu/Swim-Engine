#include "Engine/Assets/AssetSystem.h"

#include <algorithm>

namespace Swim::Assets
{

	AssetSystem::~AssetSystem()
	{
		if (running && IsOwnerThread())
		{
			Shutdown();
		}
	}

	bool AssetSystem::Initialize()
	{
		if (running)
		{
			return false;
		}
		ownerThread = std::this_thread::get_id();
		running = true;
		return true;
	}

	void AssetSystem::Shutdown()
	{
		RequireOwnerThread();
		contentIndex.clear();
		reverseDependencies.clear();
		records.clear();
		database.Clear();
		running = false;
		ownerThread = {};
	}

	AssetStatus AssetSystem::GetStatus(AssetId id) const
	{
		RequireOwnerThread();
		const auto record = records.find(id);
		return record == records.end() ? AssetStatus{} : MakeStatus(record->second);
	}

	AssetId AssetSystem::FindByContentHash(const ContentHash& contentHash) const
	{
		RequireOwnerThread();
		if (contentHash.IsZero())
		{
			return {};
		}
		const auto existing = contentIndex.find(contentHash);
		if (existing == contentIndex.end() || existing->second.empty())
		{
			return {};
		}

		return *std::min_element(existing->second.begin(), existing->second.end(), [](AssetId left, AssetId right)
		{
			return left.Value < right.Value;
		});
	}

	std::vector<AssetId> AssetSystem::GetDependents(AssetId dependency) const
	{
		RequireOwnerThread();
		std::vector<AssetId> result;
		const auto existing = reverseDependencies.find(dependency);
		if (existing == reverseDependencies.end())
		{
			return result;
		}

		result.assign(existing->second.begin(), existing->second.end());
		std::sort(result.begin(), result.end(), [](AssetId left, AssetId right)
		{
			return left.Value < right.Value;
		});
		return result;
	}

	std::size_t AssetSystem::GetDeclaredCount() const
	{
		RequireOwnerThread();
		return static_cast<std::size_t>(std::count_if(records.begin(), records.end(), [](const auto& entry)
		{
			return entry.second.Declared;
		}));
	}

	void AssetSystem::RequireOwnerThread() const
	{
		if (!running)
		{
			throw std::logic_error("AssetSystem is not initialized.");
		}
		if (ownerThread != std::this_thread::get_id())
		{
			throw std::logic_error("AssetSystem registry access must occur on its owner thread.");
		}
	}

	bool AssetSystem::SetDependenciesInternal(Record& record, std::span<const AssetId> dependencies)
	{
		std::vector<AssetId> normalized;
		normalized.reserve(dependencies.size());
		for (AssetId dependency : dependencies)
		{
			if (!dependency.IsValid() || dependency == record.Id || HasDependencyPath(dependency, record.Id))
			{
				return false;
			}
			if (std::find(normalized.begin(), normalized.end(), dependency) == normalized.end())
			{
				normalized.push_back(dependency);
			}
		}

		RemoveDependencyEdges(record);
		record.Dependencies = std::move(normalized);
		for (AssetId dependency : record.Dependencies)
		{
			reverseDependencies[dependency].insert(record.Id);
		}
		return true;
	}

	void AssetSystem::RemoveDependencyEdges(const Record& record)
	{
		for (AssetId dependency : record.Dependencies)
		{
			auto reverse = reverseDependencies.find(dependency);
			if (reverse == reverseDependencies.end())
			{
				continue;
			}
			reverse->second.erase(record.Id);
			if (reverse->second.empty())
			{
				reverseDependencies.erase(reverse);
			}
		}
	}

	bool AssetSystem::HasDependencyPath(AssetId start, AssetId target) const
	{
		std::vector<AssetId> pending{ start };
		std::unordered_set<AssetId> visited;

		while (!pending.empty())
		{
			const AssetId current = pending.back();
			pending.pop_back();
			if (current == target)
			{
				return true;
			}
			if (!visited.insert(current).second)
			{
				continue;
			}

			const auto record = records.find(current);
			if (record == records.end() || !record->second.Declared)
			{
				continue;
			}
			pending.insert(pending.end(), record->second.Dependencies.begin(), record->second.Dependencies.end());
		}
		return false;
	}

	void AssetSystem::SetContentHashInternal(Record& record, const ContentHash& contentHash)
	{
		if (!record.Hash.IsZero())
		{
			auto existing = contentIndex.find(record.Hash);
			if (existing != contentIndex.end())
			{
				existing->second.erase(record.Id);
				if (existing->second.empty())
				{
					contentIndex.erase(existing);
				}
			}
		}

		record.Hash = contentHash;
		if (!contentHash.IsZero())
		{
			contentIndex[contentHash].insert(record.Id);
		}
	}

	AssetStatus AssetSystem::MakeStatus(const Record& record) const
	{
		AssetStatus status{};
		status.Id = record.Id;
		status.Generation = record.Generation;
		status.State = record.State;
		status.Hash = record.Hash;
		status.Dependencies = record.Dependencies;
		status.Error = record.Error;
		status.Declared = record.Declared;
		if (const auto path = database.FindPath(record.Id))
		{
			status.LogicalPath = *path;
		}
		return status;
	}

}
