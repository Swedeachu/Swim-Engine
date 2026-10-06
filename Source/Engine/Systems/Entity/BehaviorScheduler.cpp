#include "Engine/Systems/Entity/BehaviorScheduler.h"

#include "Engine/Systems/Entity/Behavior.h"

namespace Engine
{

	void BehaviorScheduler::Add(Behavior& behavior, EngineState enabledStates, BehaviorTraits traits)
	{
		if (behavior.link.Scheduled)
		{
			return;
		}

		// Read once: whether a behaviour runs on wall-clock time is part of its type's contract.
		const Slot slot{ &behavior, enabledStates, behavior.UsesRealTime() };
		behavior.link.Scheduled = true;
		behavior.link.Slots[AllList] = static_cast<std::uint32_t>(all.size());
		all.push_back(slot);
		all.back().NeedsInit = !behavior.HasInited();
		pendingInits += all.back().NeedsInit ? 1u : 0u;

		if (traits.Update)
		{
			behavior.link.Slots[UpdateList] = static_cast<std::uint32_t>(update.size());
			update.push_back(slot);
		}

		if (traits.FixedUpdate)
		{
			behavior.link.Slots[FixedList] = static_cast<std::uint32_t>(fixed.size());
			fixed.push_back(slot);
		}
	}

	void BehaviorScheduler::Remove(Behavior& behavior)
	{
		if (!behavior.link.Scheduled)
		{
			return;
		}

		for (std::uint32_t list = 0; list < ListCount; ++list)
		{
			const std::uint32_t index = behavior.link.Slots[list];

			if (index == Behavior::NoSlot)
			{
				continue;
			}

			if (list == AllList && List(list)[index].NeedsInit)
			{
				List(list)[index].NeedsInit = false;
				--pendingInits;
			}

			List(list)[index].Instance = nullptr;
			behavior.link.Slots[list] = Behavior::NoSlot;
			++holes[list];
		}

		behavior.link.Scheduled = false; // Compacted lazily before the next outermost run.
	}

	void BehaviorScheduler::SetEnabledStates(Behavior& behavior, EngineState enabledStates)
	{
		for (std::uint32_t list = 0; list < ListCount; ++list)
		{
			if (const std::uint32_t index = behavior.link.Slots[list]; index != Behavior::NoSlot)
			{
				List(list)[index].States = enabledStates;
			}
		}
	}

	void BehaviorScheduler::Clear()
	{
		for (auto& slot : all)
		{
			if (slot.Instance)
			{
				slot.Instance->link.Scheduled = false;
				slot.Instance->link.Slots[AllList] = slot.Instance->link.Slots[UpdateList] = slot.Instance->link.Slots[FixedList] = Behavior::NoSlot;
			}
		}

		all.clear();
		update.clear();
		fixed.clear();
		holes[AllList] = holes[UpdateList] = holes[FixedList] = 0;
		pendingInits = 0;
	}

	void BehaviorScheduler::InitPending(EngineState state)
	{
		if (pendingInits == 0)
		{
			return;
		}

		Run(all,
			[&](Slot& slot)
			{
				if (!slot.NeedsInit || !HasAnyEngineStates(slot.States, state))
				{
					return;
				}

				slot.NeedsInit = false;
				--pendingInits;
				slot.Instance->InitIfNeeded();
			});
	}

	void BehaviorScheduler::RunUpdate(EngineState state, double dt, double realDelta)
	{
		InitPending(state);
		Run(update,
			[&](Slot& slot)
			{
				if (!HasAnyEngineStates(slot.States, state))
				{
					return;
				}

				Behavior& behavior = *slot.Instance;
				const double delta = slot.RealTime ? realDelta : dt;
				behavior.InitIfNeeded();
				behavior.Update(delta);
			});
	}

	void BehaviorScheduler::RunFixedUpdate(EngineState state, unsigned int tick)
	{
		InitPending(state);
		Run(fixed,
			[&](Slot& slot)
			{
				if (!HasAnyEngineStates(slot.States, state))
				{
					return;
				}

				Behavior& behavior = *slot.Instance;
				behavior.InitIfNeeded();
				behavior.FixedUpdate(tick);
			});
	}

	void BehaviorScheduler::Compact()
	{
		// Stable compaction (attach order is the deterministic run order), re-indexing the
		// behaviours that moved. Only runs when something was removed.
		for (std::uint32_t list = 0; list < ListCount; ++list)
		{
			if (holes[list] == 0)
			{
				continue;
			}

			auto& slots = List(list);
			std::size_t write = 0;

			for (std::size_t read = 0; read < slots.size(); ++read)
			{
				if (!slots[read].Instance)
				{
					continue;
				}

				if (write != read)
				{
					slots[write] = slots[read];
				}

				slots[write].Instance->link.Slots[list] = static_cast<std::uint32_t>(write);
				++write;
			}

			slots.resize(write);
			holes[list] = 0;
		}
	}

} // namespace Engine
