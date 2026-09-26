#pragma once

#include "Engine/EngineState.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Engine
{
	// The engine's Playing / Paused / Stopped state (Phase 22). Owned by SwimEngine and
	// shared with scenes and systems by reference; transitions are explicit calls, never
	// flag arithmetic. Listeners run synchronously, in subscription order, after the
	// state changed (so they observe the new state).
	//
	//   Stopped --Play--> Playing --Pause--> Paused --Resume--> Playing
	//      ^                 |                 |
	//      +------Stop-------+-------Stop------+
	//
	// Play while Paused resumes; Pause while Stopped is ignored.
	class EngineStateMachine
	{
	  public:
		using Listener = std::function<void(EngineState previous, EngineState current)>;
		using ListenerId = std::uint32_t;

		explicit EngineStateMachine(EngineState initial = EngineState::Playing);

		EngineState Get() const { return state; }

		bool IsPlaying() const { return state == EngineState::Playing; }

		bool IsPaused() const { return state == EngineState::Paused; }

		bool IsStopped() const { return state == EngineState::Stopped; }

		// Each returns true when the state changed.
		bool Play();
		bool Pause();
		bool Resume();
		bool Stop();
		bool TogglePause();
		// Moves to an explicit single state (Playing, Paused or Stopped); throws
		// std::invalid_argument for None or a mask.
		bool Set(EngineState target);

		ListenerId Subscribe(Listener listener);
		bool Unsubscribe(ListenerId id);

		// Transitions performed so far (diagnostics, tests).
		std::uint64_t GetTransitionCount() const { return transitions; }

	  private:
		bool Transition(EngineState target);

		struct Entry
		{
			ListenerId Id = 0;
			Listener Callback;
		};

		EngineState state = EngineState::Playing;
		std::vector<Entry> listeners;
		ListenerId nextId = 1;
		std::uint64_t transitions = 0;
	};
} // namespace Engine
