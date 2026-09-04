#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace Engine
{

	template<typename Handle, typename TValue>
	class GenerationalHandleTable
	{
	public:

		Handle Insert(TValue value)
		{
			std::uint32_t index = 0;
			if (!freeIndices.empty())
			{
				index = freeIndices.back();
				freeIndices.pop_back();

				Slot& slot = slots[index];
				slot.Data = std::move(value);
				slot.Occupied = true;
				return Handle{ index, slot.Generation };
			}

			index = static_cast<std::uint32_t>(slots.size());
			slots.push_back(Slot{ std::move(value), 1u, true });
			return Handle{ index, 1u };
		}

		TValue* Get(Handle handle)
		{
			if (!IsValid(handle))
			{
				return nullptr;
			}
			return &slots[handle.Index].Data;
		}

		const TValue* Get(Handle handle) const
		{
			if (!IsValid(handle))
			{
				return nullptr;
			}
			return &slots[handle.Index].Data;
		}

		bool IsValid(Handle handle) const
		{
			return handle.IsValid()
				&& handle.Index < slots.size()
				&& slots[handle.Index].Occupied
				&& slots[handle.Index].Generation == handle.Generation;
		}

		bool Remove(Handle handle, TValue& value)
		{
			if (!IsValid(handle))
			{
				return false;
			}

			Slot& slot = slots[handle.Index];
			value = std::move(slot.Data);
			slot.Data = TValue{};
			slot.Occupied = false;
			slot.Generation++;
			if (slot.Generation == 0)
			{
				slot.Generation = 1;
			}
			freeIndices.push_back(handle.Index);
			return true;
		}

		template<typename Fn>
		void ForEach(Fn&& fn)
		{
			for (std::uint32_t index = 0; index < slots.size(); index++)
			{
				Slot& slot = slots[index];
				if (slot.Occupied)
				{
					fn(Handle{ index, slot.Generation }, slot.Data);
				}
			}
		}

		std::size_t Size() const
		{
			return slots.size() - freeIndices.size();
		}

		void Reset()
		{
			slots.clear();
			freeIndices.clear();
		}

	private:

		struct Slot
		{
			TValue Data{};
			std::uint32_t Generation = 1;
			bool Occupied = false;
		};

		std::vector<Slot> slots;
		std::vector<std::uint32_t> freeIndices;
	};

}
