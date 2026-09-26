#pragma once

#include "Behavior.h"

#include <memory>
#include <type_traits>
#include <vector>

namespace Engine
{
	// The behaviours of one entity and the engine states they run in. Behaviours are
	// the OOP scripting layer (player controllers, cameras, managers); bulk simulation
	// (physics, rendering, particles) stays data-driven.
	struct BehaviorComponents
	{
		std::vector<std::unique_ptr<Behavior>> behaviors;

		BehaviorComponents() = default;
		BehaviorComponents(const BehaviorComponents&) = delete;
		BehaviorComponents& operator=(const BehaviorComponents&) = delete;
		BehaviorComponents(BehaviorComponents&&) noexcept = default;
		BehaviorComponents& operator=(BehaviorComponents&&) noexcept = default;

		void Add(std::unique_ptr<Behavior> behavior) { behaviors.emplace_back(std::move(behavior)); }

		// Exactly the states these behaviours run in (default: Playing). A camera
		// controller that must work while paused uses Playing | Paused.
		void SetEnabledStates(EngineState states) { enabledStates = states; }
		void AddEnabledStates(EngineState states) { enabledStates |= states; }
		void RemoveEnabledStates(EngineState states) { enabledStates &= ~states; }
		EngineState GetEnabledStates() const { return enabledStates; }

		bool IsEnabledIn(EngineState state) const { return HasAnyEngineStates(enabledStates, state); }

		// Whether the behaviours run while the engine is in `current` (one state).
		bool CanExecute(EngineState current) const { return IsEnabledIn(current); }

		EngineState enabledStates = EngineState::Playing;
	};
} // namespace Engine
