#pragma once

#include "Engine/Machine.h"

#include <type_traits>

namespace Engine
{

	class Behavior;

	// Which per-tick hooks a behaviour type implements. The scheduler only lists a
	// behaviour in the phases it overrides, so a behaviour that only reacts to collisions or
	// state changes costs nothing per frame. Computed at compile time from the type
	// (BehaviorTraitsOf<T>), so gameplay code declares nothing: overriding Update is enough.
	struct BehaviorTraits
	{
		bool Update = true;
		bool FixedUpdate = true;

		static constexpr BehaviorTraits All() { return {}; }
	};

	// A hook is "implemented" when T (or a base between T and Machine) declares it: then
	// &T::Update is a member of that class instead of Machine.
	template <typename T> constexpr BehaviorTraits BehaviorTraitsOf()
	{
		BehaviorTraits traits;
		traits.Update = !std::is_same_v<decltype(&T::Update), void (Machine::*)(double)>;
		traits.FixedUpdate = !std::is_same_v<decltype(&T::FixedUpdate), void (Machine::*)(unsigned int)>;
		return traits;
	}

} // namespace Engine
