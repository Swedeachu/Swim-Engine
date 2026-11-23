#include "Engine/Systems/Scene/SceneTimers.h"

#include <algorithm>

namespace Engine
{

	TimerHandle SceneTimers::Add(Entry entry)
	{
		std::uint32_t index = 0;

		if (!freeList.empty())
		{
			index = freeList.back();
			freeList.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(entries.size());
			entries.emplace_back();
		}

		auto& slot = entries[index];
		const auto generation = slot.Generation + 1 == 0 ? 1u : slot.Generation + 1;
		entry.Generation = generation;
		entry.Active = true;
		entry.AddedTick = ticks;
		slot = std::move(entry);
		return { index, generation };
	}

	void SceneTimers::Release(Entry& entry)
	{
		const auto index = static_cast<std::uint32_t>(&entry - entries.data());
		entry.Active = false;
		entry.Callback = {};
		entry.Predicate = {};
		entry.Task = {};
		entry.OwnerToken.reset();
		freeList.push_back(index);
	}

	TimerHandle SceneTimers::After(double seconds, std::function<void()> callback, Owner owner)
	{
		Entry entry;
		entry.Due = time + std::max(seconds, 0.0);
		entry.Callback = std::move(callback);
		entry.Owned = !owner.expired();
		entry.OwnerToken = std::move(owner);
		return Add(std::move(entry));
	}

	TimerHandle SceneTimers::Every(double interval, std::function<void()> callback, Owner owner)
	{
		Entry entry;
		entry.Interval = std::max(interval, 1.0e-4);
		entry.Due = time + entry.Interval;
		entry.Callback = std::move(callback);
		entry.Owned = !owner.expired();
		entry.OwnerToken = std::move(owner);
		return Add(std::move(entry));
	}

	TimerHandle SceneTimers::When(std::function<bool()> predicate, std::function<void()> callback, Owner owner)
	{
		Entry entry;
		entry.Predicate = std::move(predicate);
		entry.Callback = std::move(callback);
		entry.Owned = !owner.expired();
		entry.OwnerToken = std::move(owner);
		return Add(std::move(entry));
	}

	TimerHandle SceneTimers::Start(SceneTask task, Owner owner)
	{
		if (!task.handle)
		{
			return {};
		}

		Entry entry;
		entry.Due = time; // First runs on the next tick.
		entry.Task = std::move(task);
		entry.Owned = !owner.expired();
		entry.OwnerToken = std::move(owner);
		return Add(std::move(entry));
	}

	bool SceneTimers::Cancel(TimerHandle timer)
	{
		if (timer.Index >= entries.size() || !entries[timer.Index].Active || entries[timer.Index].Generation != timer.Generation)
		{
			return false;
		}

		Release(entries[timer.Index]);
		return true;
	}

	bool SceneTimers::IsActive(TimerHandle timer) const
	{
		return timer.Index < entries.size() && entries[timer.Index].Active && entries[timer.Index].Generation == timer.Generation;
	}

	std::size_t SceneTimers::GetActiveCount() const
	{
		return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const Entry& entry) { return entry.Active; }));
	}

	void SceneTimers::Clear()
	{
		entries.clear();
		freeList.clear();
	}

	void SceneTimers::Tick(double dt)
	{
		time += std::max(dt, 0.0);
		++ticks;
		// Timers added by callbacks during this tick wait for the next one.
		const auto count = entries.size();

		for (std::size_t i = 0; i < count; ++i)
		{
			if (!entries[i].Active || entries[i].AddedTick == ticks)
			{
				continue;
			}

			if (entries[i].Owned && entries[i].OwnerToken.expired())
			{
				Release(entries[i]);
				continue;
			}

			const auto generation = entries[i].Generation;

			if (entries[i].Task.handle)
			{
				auto& promise = entries[i].Task.handle.promise();
				const bool ready = promise.Until ? promise.Until() : entries[i].Due <= time;

				if (!ready)
				{
					continue;
				}

				promise.Until = {};
				promise.WakeTime = 0.0;
				const auto handle = entries[i].Task.handle;
				handle.resume(); // May add entries (reallocating): use the index afterwards.

				if (entries[i].Generation != generation || !entries[i].Active)
				{
					continue;
				}

				if (handle.done())
				{
					Release(entries[i]);
				}
				else if (!handle.promise().Until)
				{
					// Wait(seconds) stored -seconds; a next-frame wait is due now, so it runs next tick.
					entries[i].Due = time + (-handle.promise().WakeTime);
				}

				continue;
			}

			const bool ready = entries[i].Predicate ? entries[i].Predicate() : entries[i].Due <= time;

			if (!ready || entries[i].Generation != generation)
			{
				continue;
			}

			const auto callback = entries[i].Callback;

			if (entries[i].Interval > 0.0)
			{
				entries[i].Due = std::max(entries[i].Due + entries[i].Interval, time);
			}
			else
			{
				Release(entries[i]);
			}

			if (callback)
			{
				callback();
			}
		}
	}

} // namespace Engine
