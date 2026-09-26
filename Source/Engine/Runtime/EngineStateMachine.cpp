#include "Engine/Runtime/EngineStateMachine.h"

#include <algorithm>
#include <stdexcept>

namespace Engine
{
	EngineStateMachine::EngineStateMachine(EngineState initial)
	{
		if (!IsSingleEngineState(initial))
		{
			throw std::invalid_argument("The engine starts in exactly one of Playing, Paused or Stopped");
		}
		state = initial;
	}

	bool EngineStateMachine::Transition(EngineState target)
	{
		if (target == state)
		{
			return false;
		}
		const EngineState previous = state;
		state = target;
		++transitions;
		// Copy: a listener may subscribe or unsubscribe while being notified.
		const auto snapshot = listeners;
		for (const auto& entry : snapshot)
		{
			if (entry.Callback)
			{
				entry.Callback(previous, state);
			}
		}
		return true;
	}

	bool EngineStateMachine::Play()
	{
		return Transition(EngineState::Playing);
	}

	bool EngineStateMachine::Pause()
	{
		return state == EngineState::Playing && Transition(EngineState::Paused);
	}

	bool EngineStateMachine::Resume()
	{
		return state == EngineState::Paused && Transition(EngineState::Playing);
	}

	bool EngineStateMachine::Stop()
	{
		return Transition(EngineState::Stopped);
	}

	bool EngineStateMachine::TogglePause()
	{
		if (state == EngineState::Playing)
		{
			return Pause();
		}
		if (state == EngineState::Paused)
		{
			return Resume();
		}
		return false;
	}

	bool EngineStateMachine::Set(EngineState target)
	{
		if (!IsSingleEngineState(target))
		{
			throw std::invalid_argument("EngineStateMachine::Set needs Playing, Paused or Stopped");
		}
		return Transition(target);
	}

	EngineStateMachine::ListenerId EngineStateMachine::Subscribe(Listener listener)
	{
		const ListenerId id = nextId++;
		listeners.push_back({ id, std::move(listener) });
		return id;
	}

	bool EngineStateMachine::Unsubscribe(ListenerId id)
	{
		const auto before = listeners.size();
		std::erase_if(listeners,
			[id](const Entry& entry)
			{
				return entry.Id == id;
			});
		return listeners.size() != before;
	}
} // namespace Engine
