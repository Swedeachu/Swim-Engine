#include "Engine/Runtime/SimulationClock.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>

using Engine::SimulationClock;
using Engine::SimulationDomain;

SWIM_TEST("Engine.SimulationClock", "FixedStepsAccumulateRealTime")
{
	SimulationClock clock;
	clock.SetFixedRate(50.0); // 20 ms.
	auto frame = clock.Advance(0.05);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 2u);
	SWIM_CHECK_NEAR(frame.Alpha, 0.5, 1e-9);
	SWIM_CHECK_NEAR(frame.ScaledDelta, 0.05, 1e-12);
	frame = clock.Advance(0.01);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 1u);
	SWIM_CHECK_NEAR(frame.Alpha, 0.0, 1e-9);
	SWIM_CHECK_EQUAL(clock.GetFixedStepCount(), std::uint64_t{ 3 });
	SWIM_CHECK_NEAR(clock.GetSimulatedSeconds(), 0.06, 1e-12);
	SWIM_CHECK_EQUAL(clock.GetFrameCount(), std::uint64_t{ 2 });
}

SWIM_TEST("Engine.SimulationClock", "TimeScaleSlowsAndFreezesSimulation")
{
	SimulationClock clock;
	clock.SetFixedRate(100.0);
	clock.SetTimeScale(0.5);
	auto frame = clock.Advance(0.1);
	SWIM_CHECK_NEAR(frame.RealDelta, 0.1, 1e-12);
	SWIM_CHECK_NEAR(frame.ScaledDelta, 0.05, 1e-12);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 5u);
	clock.SetTimeScale(0.0);
	frame = clock.Advance(0.1);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 0u);
	SWIM_CHECK_NEAR(frame.ScaledDelta, 0.0, 1e-12);
	SWIM_CHECK_THROWS(clock.SetTimeScale(-1.0), std::invalid_argument);
	SWIM_CHECK_THROWS(clock.SetFixedRate(0.0), std::invalid_argument);
}

SWIM_TEST("Engine.SimulationClock", "PauseStopsTimeAndStepsAdvanceExactlyOneTick")
{
	SimulationClock clock;
	clock.SetFixedRate(60.0);
	clock.SetPaused(true);
	auto frame = clock.Advance(0.1);
	SWIM_CHECK(frame.Paused);
	SWIM_CHECK(!frame.Stepped);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 0u);
	SWIM_CHECK_NEAR(frame.ScaledDelta, 0.0, 1e-12);
	SWIM_CHECK_NEAR(frame.RealDelta, 0.1, 1e-12); // Wall clock keeps running.

	clock.Step(2);
	SWIM_CHECK_EQUAL(clock.GetPendingSteps(), 2u);
	frame = clock.Advance(0.1);
	SWIM_CHECK(frame.Stepped);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 1u);
	SWIM_CHECK_NEAR(frame.ScaledDelta, clock.GetFixedDelta(), 1e-12);
	frame = clock.Advance(0.0);
	SWIM_CHECK(frame.Stepped);
	frame = clock.Advance(0.1);
	SWIM_CHECK(!frame.Stepped);
	SWIM_CHECK_EQUAL(clock.GetFixedStepCount(), std::uint64_t{ 2 });

	// Steps are only queued while paused, and resuming drops leftovers.
	clock.Step(3);
	clock.SetPaused(false);
	SWIM_CHECK_EQUAL(clock.GetPendingSteps(), 0u);
	clock.Step(3);
	SWIM_CHECK_EQUAL(clock.GetPendingSteps(), 0u);
}

SWIM_TEST("Engine.SimulationClock", "LongFramesAreClampedAndExcessStepsDropped")
{
	SimulationClock clock;
	clock.SetFixedRate(100.0);
	clock.SetMaxFrameDelta(0.25);
	clock.SetMaxFixedSteps(8);
	const auto frame = clock.Advance(5.0); // A hitch.
	SWIM_CHECK_NEAR(frame.RealDelta, 0.25, 1e-12);
	SWIM_CHECK_EQUAL(frame.FixedSteps, 8u);
	SWIM_CHECK(clock.GetDroppedSeconds() > 0.16);
	SWIM_CHECK(frame.Alpha < 1.0);
	// Invalid deltas count as zero.
	const auto bad = clock.Advance(-1.0);
	SWIM_CHECK_NEAR(bad.RealDelta, 0.0, 1e-12);
}

SWIM_TEST("Engine.SimulationClock", "DomainsFollowSimulationOrRealTime")
{
	SimulationClock clock;
	clock.SetTimeScale(0.25);
	clock.SetFollowsSimulation(SimulationDomain::Audio, false);
	const auto frame = clock.Advance(0.1);
	SWIM_CHECK_NEAR(clock.GetDelta(SimulationDomain::Particles, frame), 0.025, 1e-12);
	SWIM_CHECK_NEAR(clock.GetDelta(SimulationDomain::Audio, frame), 0.1, 1e-12);
	SWIM_CHECK(clock.FollowsSimulation(SimulationDomain::Physics));
	SWIM_CHECK(!clock.FollowsSimulation(SimulationDomain::Audio));
}
