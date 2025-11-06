#pragma once

#include <memory>
#include <vector>
#include "Behavior.h"

namespace Engine
{

	// Note: yes this completely defeats the purpose of an optimzied data driven ECS, but behaviors are intended as close control OOP stuff that won't really ever impact performance. 
	// For example the player controller, or a gameplay score manager, or the simple ticking of a behavior tree, etc.
	// Of course physics updates and rendering will always be fully data driven, while the behavior scripting part of the engine is traditonal virtual OOP like this in derived Behavior classes.
	// I'm not worried because scenarios where a lot of behavior among entities would happen is already traditionally programmed as data driven, such as swarms and flocking. 

	// A wrapper for all the behavior components on an entity. We iterate all the BehaviorComponents in the scene's registry each frame for behavior updates.
	struct BehaviorComponents
	{
		std::vector<std::unique_ptr<Behavior>> behaviors;

		BehaviorComponents() = default;
		BehaviorComponents(const BehaviorComponents&) = delete;
		BehaviorComponents& operator=(const BehaviorComponents&) = delete;
		BehaviorComponents(BehaviorComponents&&) noexcept = default;
		BehaviorComponents& operator=(BehaviorComponents&&) noexcept = default;

		// Adds a new behavior to the list
		void Add(std::unique_ptr<Behavior> behavior)
		{
			behaviors.emplace_back(std::move(behavior));
		}

		// Set exactly which engine states these behaviors are enabled in.
		// Default is EngineState::Playing.
		void SetEnabledStates(EngineState states) { enabledStates = states; }

		// Add one or more states to the enable mask.
		void AddEnabledStates(EngineState states) { enabledStates |= states; }

		// Remove one or more states from the enable mask.
		void RemoveEnabledStates(EngineState states)
		{
			enabledStates = static_cast<EngineState>(
				static_cast<std::underlying_type_t<EngineState>>(enabledStates) &
				~static_cast<std::underlying_type_t<EngineState>>(states)
				);
		}

		// Query which states are enabled for this behavior (bitmask).
		EngineState GetEnabledStates() const { return enabledStates; }

		// Convenient predicate: is this behavior enabled in a specific state?
		bool IsEnabledIn(EngineState state) const
		{
			return HasAny(enabledStates, state);
		}

		// Main gate: can this behavior execute given current engine state?
		// Typical use: if (!behavior->CanExecute(currentEngineState)) return;
		bool CanExecute(EngineState currentEngineState) const
		{
			return HasAny(enabledStates, currentEngineState);
		}

		// Which engine states this behavior is active in (bitmask)
		// Default: active only while Playing
		EngineState enabledStates = EngineState::Playing;

	};

}
