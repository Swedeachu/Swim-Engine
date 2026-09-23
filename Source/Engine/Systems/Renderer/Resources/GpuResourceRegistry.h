#pragma once

#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistryDesc.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistryStats.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Swim::Render
{
	// Generational registry for persistent renderer resources with timeline-safe
	// retirement. Release invalidates the handle immediately, but the record (and
	// any RHI objects it owns) stays alive until the supplied timeline point
	// completes, and its slot index is not reused until then. That makes indices
	// safe to mirror into GPU tables (metadata rows, bindless elements) while work
	// that referenced the old contents is still in flight.
	//
	// Externally synchronized; nothing blocks except Drain. Timelines passed to
	// Release must outlive their pending entries (Drain before destroying them).
	// Record must be movable; RHI ownership is normally unique_ptr members.
	template <typename Tag, typename Record> class GpuResourceRegistry
	{
	  public:
		using Handle = GpuHandle<Tag>;

		explicit GpuResourceRegistry(const GpuResourceRegistryDesc& desc = {}) : maxSlots(desc.MaxSlots), name(desc.DebugName)
		{
			if (maxSlots == 0 || maxSlots >= Handle::InvalidIndex)
			{
				throw std::invalid_argument("GpuResourceRegistry needs between 1 and UINT32_MAX - 1 slots");
			}
		}

		// Best effort: waits pending retirements before destroying their records.
		// Call Drain explicitly to observe wait failures.
		~GpuResourceRegistry()
		{
			try
			{
				Drain();
			}
			catch (...)
			{
			}
		}

		GpuResourceRegistry(const GpuResourceRegistry&) = delete;
		GpuResourceRegistry& operator=(const GpuResourceRegistry&) = delete;

		// Empty when every slot is live or still retiring. Retired slots are reused
		// oldest-first, which maximizes the time before an index is recycled.
		std::optional<Handle> TryCreate(Record record)
		{
			std::uint32_t index = 0;
			if (!freeSlots.empty())
			{
				index = freeSlots.front();
				freeSlots.pop_front();
			}
			else if (slots.size() < maxSlots)
			{
				index = static_cast<std::uint32_t>(slots.size());
				slots.emplace_back();
			}
			else
			{
				return std::nullopt;
			}

			auto& slot = slots[index];
			slot.Data.emplace(std::move(record));
			slot.State = SlotState::Live;
			++live;
			return Handle{ index, slot.Generation };
		}

		Handle Create(Record record)
		{
			auto handle = TryCreate(std::move(record));
			if (!handle)
			{
				throw std::length_error(name + " has no free GPU resource slots");
			}
			return *handle;
		}

		bool IsValid(Handle handle) const
		{
			return handle.IsValid() && handle.Index < slots.size() && slots[handle.Index].State == SlotState::Live &&
				slots[handle.Index].Generation == handle.Generation;
		}

		Record* Get(Handle handle) { return IsValid(handle) ? &*slots[handle.Index].Data : nullptr; }

		const Record* Get(Handle handle) const { return IsValid(handle) ? &*slots[handle.Index].Data : nullptr; }

		Record& GetChecked(Handle handle)
		{
			if (auto* record = Get(handle))
			{
				return *record;
			}
			throw std::invalid_argument(name + " handle is invalid, stale or released");
		}

		// Invalidates the handle now; retires the record after lastUse completes.
		// A null timeline means no submitted GPU work references the record, so it
		// retires at the next collection. Returns false for invalid/stale handles.
		bool Release(Handle handle, Rhi::TimelinePoint lastUse = {})
		{
			if (!lastUse.Semaphore && lastUse.Value != 0)
			{
				throw std::invalid_argument("GpuResourceRegistry retirement value needs a timeline");
			}
			if (!IsValid(handle))
			{
				return false;
			}

			auto& slot = slots[handle.Index];
			pending.push_back({ handle, lastUse, std::move(*slot.Data) });
			slot.Data.reset();
			slot.State = SlotState::Retiring;
			++slot.Generation; // Zero after 2^32 - 1 uses: retired permanently, never wrapped.
			--live;
			return true;
		}

		// Nonblocking. Destroys records whose last use completed and frees their
		// slots. onRetired(Handle released, Record&) runs before destruction, for
		// example to return GeometryHeap ranges. If it throws, that entry stays
		// pending and the exception propagates; earlier entries remain retired.
		template <typename OnRetired> std::size_t CollectRetired(OnRetired&& onRetired)
		{
			std::size_t retired = 0;
			std::size_t i = 0;
			try
			{
				for (; i < pending.size(); ++i)
				{
					auto& entry = pending[i];
					if (!IsComplete(entry.LastUse))
					{
						continue;
					}
					onRetired(entry.Released, entry.Data);
					FinishRetirement(entry);
					entry.Released = {}; // Marks the entry for removal.
					++retired;
				}
			}
			catch (...)
			{
				RemoveRetired();
				throw;
			}
			RemoveRetired();
			return retired;
		}

		std::size_t CollectRetired()
		{
			return CollectRetired(
				[](Handle, Record&)
				{
				});
		}

		// Waits for every pending timeline point, then retires everything. On a
		// failed wait nothing further is retired and std::runtime_error is thrown.
		template <typename OnRetired> std::size_t Drain(OnRetired&& onRetired)
		{
			for (const auto& entry : pending)
			{
				if (!IsComplete(entry.LastUse) && !entry.LastUse.Semaphore->Wait(entry.LastUse.Value))
				{
					throw std::runtime_error(name + " retirement wait failed");
				}
			}
			return CollectRetired(std::forward<OnRetired>(onRetired));
		}

		std::size_t Drain()
		{
			return Drain(
				[](Handle, Record&)
				{
				});
		}

		template <typename Fn> void ForEach(Fn&& fn)
		{
			for (std::uint32_t index = 0; index < slots.size(); ++index)
			{
				if (slots[index].State == SlotState::Live)
				{
					fn(Handle{ index, slots[index].Generation }, *slots[index].Data);
				}
			}
		}

		template <typename Fn> void ForEach(Fn&& fn) const
		{
			for (std::uint32_t index = 0; index < slots.size(); ++index)
			{
				if (slots[index].State == SlotState::Live)
				{
					fn(Handle{ index, slots[index].Generation }, *slots[index].Data);
				}
			}
		}

		GpuResourceRegistryStats GetStats() const
		{
			return { live, static_cast<std::uint32_t>(pending.size()), static_cast<std::uint32_t>(freeSlots.size()),
				static_cast<std::uint32_t>(slots.size()), exhausted, maxSlots };
		}

	  private:
		enum class SlotState : std::uint8_t
		{
			Free,
			Live,
			Retiring,
			Exhausted
		};

		struct Slot
		{
			std::optional<Record> Data;
			std::uint32_t Generation = 1;
			SlotState State = SlotState::Free;
		};

		struct Pending
		{
			Handle Released;
			Rhi::TimelinePoint LastUse;
			Record Data;
		};

		static bool IsComplete(const Rhi::TimelinePoint& point)
		{
			return !point.Semaphore || point.Semaphore->GetCompletedValue() >= point.Value;
		}

		void FinishRetirement(Pending& entry)
		{
			{
				[[maybe_unused]] Record discarded = std::move(entry.Data); // Destroy owned GPU objects now.
			}
			auto& slot = slots[entry.Released.Index];
			if (slot.Generation == 0)
			{
				slot.State = SlotState::Exhausted;
				++exhausted;
			}
			else
			{
				slot.State = SlotState::Free;
				freeSlots.push_back(entry.Released.Index);
			}
		}

		void RemoveRetired()
		{
			std::erase_if(pending,
				[](const Pending& entry)
				{
					return !entry.Released.IsValid();
				});
		}

		std::vector<Slot> slots;
		std::deque<std::uint32_t> freeSlots;
		std::vector<Pending> pending;
		std::uint32_t maxSlots;
		std::uint32_t live = 0;
		std::uint32_t exhausted = 0;
		std::string name;
	};
} // namespace Swim::Render
