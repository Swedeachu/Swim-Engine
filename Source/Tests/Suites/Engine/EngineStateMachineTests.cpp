#include "Engine/Runtime/EngineStateMachine.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>
#include <utility>
#include <vector>

using Engine::EngineState;
using Engine::EngineStateMachine;

SWIM_TEST("Engine.StateMachine", "StartsInOneStateAndRejectsMasks")
{
	EngineStateMachine machine;
	SWIM_CHECK(machine.IsPlaying());
	SWIM_CHECK_EQUAL(machine.GetTransitionCount(), std::uint64_t{ 0 });
	SWIM_CHECK(EngineStateMachine(EngineState::Stopped).IsStopped());
	SWIM_CHECK_THROWS(EngineStateMachine(EngineState::None), std::invalid_argument);
	SWIM_CHECK_THROWS(EngineStateMachine(EngineState::Playing | EngineState::Paused), std::invalid_argument);
	SWIM_CHECK_THROWS(machine.Set(EngineState::All), std::invalid_argument);
}

SWIM_TEST("Engine.StateMachine", "PauseAndResumeOnlyFromTheRightState")
{
	EngineStateMachine machine;
	SWIM_CHECK(!machine.Resume()); // Not paused.
	SWIM_CHECK(machine.Pause());
	SWIM_CHECK(machine.IsPaused());
	SWIM_CHECK(!machine.Pause()); // Already paused.
	SWIM_CHECK(machine.Resume());
	SWIM_CHECK(machine.IsPlaying());
	SWIM_CHECK(machine.Stop());
	SWIM_CHECK(!machine.Pause()); // Stopped cannot pause.
	SWIM_CHECK(!machine.TogglePause());
	SWIM_CHECK(machine.Play());
	SWIM_CHECK(machine.TogglePause());
	SWIM_CHECK(machine.IsPaused());
	SWIM_CHECK(machine.TogglePause());
	SWIM_CHECK(machine.IsPlaying());
	SWIM_CHECK(!machine.Play()); // No transition to the same state.
	SWIM_CHECK_EQUAL(machine.GetTransitionCount(), std::uint64_t{ 6 });
}

SWIM_TEST("Engine.StateMachine", "ListenersSeeEveryTransitionInOrder")
{
	EngineStateMachine machine;
	std::vector<std::pair<EngineState, EngineState>> seen;
	const auto id = machine.Subscribe(
		[&](EngineState previous, EngineState current)
		{
			seen.emplace_back(previous, current);
		});
	machine.Pause();
	machine.Stop();
	machine.Play();
	SWIM_REQUIRE_EQUAL(seen.size(), std::size_t{ 3 });
	SWIM_CHECK(seen[0] == std::make_pair(EngineState::Playing, EngineState::Paused));
	SWIM_CHECK(seen[1] == std::make_pair(EngineState::Paused, EngineState::Stopped));
	SWIM_CHECK(seen[2] == std::make_pair(EngineState::Stopped, EngineState::Playing));
	SWIM_CHECK(machine.Unsubscribe(id));
	SWIM_CHECK(!machine.Unsubscribe(id));
	machine.Pause();
	SWIM_CHECK_EQUAL(seen.size(), std::size_t{ 3 });
}

SWIM_TEST("Engine.StateMachine", "ListenersMayUnsubscribeWhileNotified")
{
	EngineStateMachine machine;
	int calls = 0;
	EngineStateMachine::ListenerId self = 0;
	self = machine.Subscribe(
		[&](EngineState, EngineState)
		{
			++calls;
			machine.Unsubscribe(self);
		});
	machine.Pause();
	machine.Resume();
	SWIM_CHECK_EQUAL(calls, 1);
}

SWIM_TEST("Engine.StateMachine", "StateNamesAndTokens")
{
	SWIM_CHECK(Engine::ToString(EngineState::Playing) == "Playing");
	SWIM_CHECK(Engine::ToString(EngineState::Stopped) == "Stopped");
	SWIM_CHECK(Engine::ParseEngineStateToken(" Pause ") == EngineState::Paused);
	SWIM_CHECK(Engine::ParseEngineStateToken("play") == EngineState::Playing);
	SWIM_CHECK(Engine::ParseEngineStateToken("editing") == EngineState::None);
	SWIM_CHECK(Engine::IsSingleEngineState(EngineState::Stopped));
	SWIM_CHECK(!Engine::IsSingleEngineState(EngineState::Playing | EngineState::Stopped));
	// ~ keeps masks inside the defined states.
	SWIM_CHECK((~EngineState::Playing) == (EngineState::Paused | EngineState::Stopped));
}
