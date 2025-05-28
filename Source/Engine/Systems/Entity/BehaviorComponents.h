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
	};

}
