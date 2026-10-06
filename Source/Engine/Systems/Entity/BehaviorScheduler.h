#pragma once

#include "Engine/EngineState.h"
#include "Engine/Systems/Entity/BehaviorTraits.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace Engine
{

	class Behavior;

	// The per-scene run lists of behaviours: dense arrays of {behaviour, enabled states,
	// clock} per phase, in attach order. The hot loops walk them by index - no per-frame
	// entity snapshot, sort, registry lookup or allocation, and one pointer per call (the
	// virtual dispatch needs the object anyway). Behaviours appear only in the phases their
	// type overrides (BehaviorTraits).
	//
	// Structural changes during a run are safe: a removed behaviour's slot is cleared in
	// place (skipped, compacted before the next outermost run), and behaviours attached during a
	// run join from the next run on. Owned by Scene; gameplay code never touches it.
	class BehaviorScheduler
	{

	  public:

		void Add(Behavior& behavior, EngineState enabledStates, BehaviorTraits traits);

		void Remove(Behavior& behavior);

		// The entity's mask changed (Scene::SetEnabledStates): updates its behaviours' slots.
		void SetEnabledStates(Behavior& behavior, EngineState enabledStates);

		void Clear();

		// Update(dt) (real-time behaviours get realDelta) and FixedUpdate on every behaviour
		// that runs in `state`, initializing each on its first call.
		void RunUpdate(EngineState state, double dt, double realDelta);

		void RunFixedUpdate(EngineState state, unsigned int tick);

		// Every attached behaviour (attach order), regardless of state: f(Behavior&).
		template <typename F> void ForEach(F&& f)
		{
			Run(all,
				[&](Slot& slot)
				{
					f(*slot.Instance);
				});
		}

		// Behaviours that can run in `state`: f(Behavior&).
		template <typename F> void ForEachIn(EngineState state, F&& f)
		{
			Run(all,
				[&](Slot& slot)
				{
					if (HasAnyEngineStates(slot.States, state))
					{
						f(*slot.Instance);
					}

				});
		}

		std::size_t GetCount() const { return all.size() - holes[AllList]; }

		std::size_t GetUpdateCount() const { return update.size() - holes[UpdateList]; }

		std::size_t GetFixedUpdateCount() const { return fixed.size() - holes[FixedList]; }

	  private:

		struct Slot
		{
			Behavior* Instance = nullptr; // Null: removed (a hole until compaction).
			EngineState States = EngineState::Playing;
			bool RealTime = false;
			bool NeedsInit = false; // All-list only: not initialized yet (InitPending).
		};

		enum ListIndex : std::uint32_t
		{
			AllList,
			UpdateList,
			FixedList,
			ListCount
		};

		std::vector<Slot>& List(std::uint32_t index) { return index == AllList ? all : (index == UpdateList ? update : fixed); }

		// Index loop over the entries that existed when the run started. Holes left by
		// removals are compacted once, before the outermost run (destroying N entities costs
		// O(N), not O(N^2)).
		template <typename F> void Run(std::vector<Slot>& list, F&& f)
		{
			if (depth == 0 && (holes[AllList] | holes[UpdateList] | holes[FixedList]) != 0)
			{
				Compact();
			}

			struct Depth
			{
				std::uint32_t& Value;

				explicit Depth(std::uint32_t& value) : Value(value) { ++Value; }

				~Depth() { --Value; }
			} guard(depth);
			const std::size_t count = list.size();

			for (std::size_t i = 0; i < count; ++i)
			{
				// Re-read through the vector each step: a callback may append (reallocate).
				if (list[i].Instance)
				{
					f(list[i]);
				}
			}
		}

		void Compact();

		// Initializes behaviours that joined since the last run and can run in `state`, so
		// hook-only behaviours (collisions, state changes) get Init before their first hook
		// without being in a per-frame list. Free while nothing is pending.
		void InitPending(EngineState state);

		std::uint32_t pendingInits = 0;

		std::vector<Slot> all;
		std::vector<Slot> update;
		std::vector<Slot> fixed;
		std::uint32_t holes[ListCount] = {};
		std::uint32_t depth = 0;

	};

} // namespace Engine
