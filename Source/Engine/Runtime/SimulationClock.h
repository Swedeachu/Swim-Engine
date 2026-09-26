#pragma once

#include <array>
#include <cstdint>

namespace Engine
{
	// Systems that advance with simulated time. Each either follows the simulation
	// (frozen while paused, scaled by the time scale) or runs on real time.
	enum class SimulationDomain : std::uint8_t
	{
		Physics,
		Animation,
		Particles,
		Behaviors,
		Audio,
		Count
	};

	// What one Advance produced.
	struct SimulationFrame
	{
		double RealDelta = 0.0;	  // Wall-clock seconds since the previous frame (clamped).
		double ScaledDelta = 0.0; // Simulated seconds this frame: RealDelta x TimeScale, 0 while paused.
		std::uint32_t FixedSteps = 0;
		double FixedDelta = 1.0 / 60.0;
		double Alpha = 0.0;	  // Leftover fraction of a fixed step (render interpolation).
		bool Paused = false;  // The simulation is frozen this frame.
		bool Stepped = false; // Paused, but one requested single step advances.
		std::uint64_t Frame = 0;
	};

	// Simulated time for the runtime (Phase 22): a fixed-step accumulator with a time
	// scale, pause, single stepping and per-domain participation. It never reads a
	// clock itself; the engine feeds it the measured frame time.
	//
	//   Advance(real)  -> FixedSteps fixed updates this frame and the variable-rate delta
	//   while paused   -> no steps and ScaledDelta 0, unless Step() queued single steps:
	//                     then exactly one fixed step per frame, with ScaledDelta = FixedDelta
	//   spiral guard   -> at most MaxFixedSteps per frame; the excess time is dropped
	//                     (GetDroppedSeconds) instead of growing without bound
	class SimulationClock
	{
	  public:
		SimulationClock();

		// Throws std::invalid_argument outside 1 .. 1000 Hz.
		void SetFixedRate(double hertz);

		double GetFixedDelta() const { return fixedDelta; }

		// Throws std::invalid_argument outside 0 .. 100 (0 freezes like a pause without pausing).
		void SetTimeScale(double scale);

		double GetTimeScale() const { return timeScale; }

		void SetPaused(bool value);

		bool IsPaused() const { return paused; }

		// Queues single fixed steps while paused (ignored while running). Capped at 1000.
		void Step(std::uint32_t steps = 1);

		std::uint32_t GetPendingSteps() const { return pendingSteps; }

		// Limits (both throw std::invalid_argument when not positive).
		void SetMaxFrameDelta(double seconds);
		void SetMaxFixedSteps(std::uint32_t steps);

		// Whether a domain follows pause/time scale (true by default for every domain).
		void SetFollowsSimulation(SimulationDomain domain, bool follows);
		bool FollowsSimulation(SimulationDomain domain) const;

		SimulationFrame Advance(double realDelta);
		// The delta a domain should integrate this frame.
		double GetDelta(SimulationDomain domain, const SimulationFrame& frame) const;

		// Drops the accumulator (camera cuts, scene loads) without touching pause state.
		void ResetAccumulator() { accumulator = 0.0; }

		double GetSimulatedSeconds() const { return simulated; }

		double GetRealSeconds() const { return real; }

		double GetDroppedSeconds() const { return dropped; }

		std::uint64_t GetFixedStepCount() const { return fixedStepCount; }

		std::uint64_t GetFrameCount() const { return frameCount; }

	  private:
		double fixedDelta = 1.0 / 60.0;
		double timeScale = 1.0;
		double maxFrameDelta = 0.25;
		std::uint32_t maxFixedSteps = 8;
		double accumulator = 0.0;
		bool paused = false;
		std::uint32_t pendingSteps = 0;
		std::array<bool, static_cast<std::size_t>(SimulationDomain::Count)> follows{};
		double simulated = 0.0;
		double real = 0.0;
		double dropped = 0.0;
		std::uint64_t fixedStepCount = 0;
		std::uint64_t frameCount = 0;
	};
} // namespace Engine
