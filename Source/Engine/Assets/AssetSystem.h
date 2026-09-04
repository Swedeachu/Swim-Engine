#pragma once

#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/AssetState.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Swim::Assets
{

	class AssetSystem
	{
	public:

		AssetSystem() = default;
		~AssetSystem();

		AssetSystem(const AssetSystem&) = delete;
		AssetSystem& operator=(const AssetSystem&) = delete;

		bool Initialize();
		void Shutdown();
		bool IsRunning() const { return running; }
		bool IsOwnerThread() const { return running && ownerThread == std::this_thread::get_id(); }

		AssetDatabase& GetDatabase() { return database; }
		const AssetDatabase& GetDatabase() const { return database; }

		template<typename T>
		AssetHandle<T> Declare(std::string_view logicalPath)
		{
			RequireOwnerThread();
			return Declare<T>(database.GetOrCreate(logicalPath));
		}

		template<typename T>
		AssetHandle<T> Declare(AssetId id)
		{
			RequireOwnerThread();
			if (!id.IsValid())
			{
				throw std::invalid_argument("Cannot declare an invalid AssetId.");
			}

			Record& record = records[id];
			if (!record.Id.IsValid())
			{
				record.Id = id;
				record.Generation = 1;
			}

			const void* type = TypeToken<T>();
			if (record.Declared && record.Type != type)
			{
				throw std::logic_error("AssetId is already declared with a different asset type.");
			}

			record.Declared = true;
			record.Type = type;
			return AssetHandle<T>(id, record.Generation);
		}

		template<typename T>
		AssetHandle<T> Find(std::string_view logicalPath) const
		{
			RequireOwnerThread();
			const auto id = database.FindId(logicalPath);
			if (!id)
			{
				return {};
			}

			const auto record = records.find(*id);
			if (record == records.end() || !record->second.Declared || record->second.Type != TypeToken<T>())
			{
				return {};
			}
			return AssetHandle<T>(*id, record->second.Generation);
		}

		template<typename T>
		bool IsCurrent(AssetHandle<T> handle) const
		{
			RequireOwnerThread();
			return FindRecord(handle) != nullptr;
		}

		template<typename T>
		bool Queue(AssetHandle<T> handle)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record || record->State == AssetLoadState::Loading || record->State == AssetLoadState::Resident)
			{
				return false;
			}
			record->State = AssetLoadState::Queued;
			record->Error = {};
			return true;
		}

		template<typename T>
		bool BeginLoading(AssetHandle<T> handle)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record || record->State == AssetLoadState::Resident || record->State == AssetLoadState::Loading)
			{
				return false;
			}
			record->State = AssetLoadState::Loading;
			record->Error = {};
			return true;
		}

		template<typename T>
		bool Publish(
			AssetHandle<T> handle,
			T asset,
			ContentHash contentHash = {},
			std::span<const AssetId> dependencies = {}
		)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record)
			{
				return false;
			}

			if (!SetDependenciesInternal(*record, dependencies))
			{
				return false;
			}
			SetContentHashInternal(*record, contentHash);
			record->Value = std::make_unique<Value<T>>(std::move(asset));
			record->State = AssetLoadState::Resident;
			record->Error = {};
			return true;
		}

		template<typename T>
		bool Fail(AssetHandle<T> handle, AssetError error)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record)
			{
				return false;
			}
			if (error.Code == AssetErrorCode::None)
			{
				error.Code = AssetErrorCode::Internal;
			}
			record->Value.reset();
			record->State = AssetLoadState::Failed;
			record->Error = std::move(error);
			return true;
		}

		template<typename T>
		bool Unload(AssetHandle<T> handle)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record)
			{
				return false;
			}
			record->Value.reset();
			record->State = AssetLoadState::Unloaded;
			record->Error = {};
			return true;
		}

		template<typename T>
		bool Forget(AssetHandle<T> handle)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record)
			{
				return false;
			}

			RemoveDependencyEdges(*record);
			SetContentHashInternal(*record, {});
			record->Value.reset();
			record->Dependencies.clear();
			record->Error = {};
			record->State = AssetLoadState::Unloaded;
			record->Declared = false;
			record->Type = nullptr;
			++record->Generation;
			if (record->Generation == 0)
			{
				record->Generation = 1;
			}
			return true;
		}

		template<typename T>
		bool SetDependencies(AssetHandle<T> handle, std::span<const AssetId> dependencies)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			return record && SetDependenciesInternal(*record, dependencies);
		}

		template<typename T>
		bool SetContentHash(AssetHandle<T> handle, ContentHash contentHash)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record)
			{
				return false;
			}
			SetContentHashInternal(*record, contentHash);
			return true;
		}

		template<typename T>
		// The returned pointer is a transient view of the current residency. Keep the
		// AssetHandle as persistent identity and Resolve() it again after any operation
		// that may republish, unload, fail, forget, or otherwise replace this asset.
		T* Resolve(AssetHandle<T> handle)
		{
			RequireOwnerThread();
			Record* record = FindRecord(handle);
			if (!record || record->State != AssetLoadState::Resident || !record->Value)
			{
				return nullptr;
			}
			return &static_cast<Value<T>*>(record->Value.get())->Data;
		}

		template<typename T>
		// Const resolution has the same lifetime contract as the mutable overload:
		// the pointer is only valid until the referenced residency is replaced.
		const T* Resolve(AssetHandle<T> handle) const
		{
			RequireOwnerThread();
			const Record* record = FindRecord(handle);
			if (!record || record->State != AssetLoadState::Resident || !record->Value)
			{
				return nullptr;
			}
			return &static_cast<const Value<T>*>(record->Value.get())->Data;
		}

		AssetStatus GetStatus(AssetId id) const;

		template<typename T>
		AssetStatus GetStatus(AssetHandle<T> handle) const
		{
			RequireOwnerThread();
			const Record* record = FindRecord(handle);
			return record ? MakeStatus(*record) : AssetStatus{};
		}

		AssetId FindByContentHash(const ContentHash& contentHash) const;
		ContentHash ComputeDependencyRevisionHash(AssetId root) const;
		std::vector<AssetId> GetDependents(AssetId dependency) const;
		std::size_t GetDeclaredCount() const;

	private:

		struct ValueBase
		{
			virtual ~ValueBase() = default;
		};

		template<typename T>
		struct Value final : ValueBase
		{
			explicit Value(T value)
				: Data(std::move(value))
			{
			}

			T Data;
		};

		struct Record
		{
			AssetId Id{};
			std::uint32_t Generation = 0;
			const void* Type = nullptr;
			AssetLoadState State = AssetLoadState::Unloaded;
			ContentHash Hash{};
			std::vector<AssetId> Dependencies;
			AssetError Error{};
			std::unique_ptr<ValueBase> Value;
			bool Declared = false;
		};

		template<typename T>
		static const void* TypeToken()
		{
			static const std::uint8_t Token = 0;
			return &Token;
		}

		template<typename T>
		Record* FindRecord(AssetHandle<T> handle)
		{
			if (!handle.IsValid())
			{
				return nullptr;
			}
			const auto existing = records.find(handle.GetId());
			if (existing == records.end())
			{
				return nullptr;
			}
			Record& record = existing->second;
			if (!record.Declared || record.Generation != handle.GetGeneration() || record.Type != TypeToken<T>())
			{
				return nullptr;
			}
			return &record;
		}

		template<typename T>
		const Record* FindRecord(AssetHandle<T> handle) const
		{
			return const_cast<AssetSystem*>(this)->FindRecord(handle);
		}

		void RequireOwnerThread() const;
		bool SetDependenciesInternal(Record& record, std::span<const AssetId> dependencies);
		void RemoveDependencyEdges(const Record& record);
		bool HasDependencyPath(AssetId start, AssetId target) const;
		void SetContentHashInternal(Record& record, const ContentHash& contentHash);
		AssetStatus MakeStatus(const Record& record) const;

		bool running = false;
		std::thread::id ownerThread{};
		AssetDatabase database;
		std::unordered_map<AssetId, Record> records;
		std::unordered_map<AssetId, std::unordered_set<AssetId>> reverseDependencies;
		std::unordered_map<ContentHash, std::unordered_set<AssetId>> contentIndex;
	};

}
