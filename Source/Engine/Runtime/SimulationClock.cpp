#include "Engine/Runtime/SimulationClock.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine
{
	SimulationClock::SimulationClock()
	{
		follows.fill(true);
	}

	void SimulationClock::SetFixedRate(double hertz)
	{
		if (!std::isfinite(hertz) || hertz < 1.0 || hertz > 1000.0)
		{
			throw std::invalid_argument("Fixed simulation rate must be 1 .. 1000 Hz");
		}
		fixedDelta = 1.0 / hertz;
	}

	void SimulationClock::SetTimeScale(double scale)
	{
		if (!std::isfinite(scale) || scale < 0.0 || scale > 100.0)
		{
			throw std::invalid_argument("Time scale must be 0 .. 100");
		}
		timeScale = scale;
	}

	void SimulationClock::SetPaused(bool value)
	{
		paused = value;
		if (!paused)
		{
			pendingSteps = 0;
		}
	}

	void SimulationClock::Step(std::uint32_t steps)
	{
		if (paused)
		{
			pendingSteps = std::min<std::uint32_t>(pendingSteps + steps, 1000u);
		}
	}

	void SimulationClock::SetMaxFrameDelta(double seconds)
	{
		if (!std::isfinite(seconds) || seconds <= 0.0)
		{
			throw std::invalid_argument("Maximum frame delta must be positive");
		}
		maxFrameDelta = seconds;
	}

	void SimulationClock::SetMaxFixedSteps(std::uint32_t steps)
	{
		if (steps == 0)
		{
			throw std::invalid_argument("At least one fixed step per frame is required");
		}
		maxFixedSteps = steps;
	}

	void SimulationClock::SetFollowsSimulation(SimulationDomain domain, bool value)
	{
		if (domain >= SimulationDomain::Count)
		{
			throw std::invalid_argument("Unknown simulation domain");
		}
		follows[static_cast<std::size_t>(domain)] = value;
	}

	bool SimulationClock::FollowsSimulation(SimulationDomain domain) const
	{
		return domain < SimulationDomain::Count && follows[static_cast<std::size_t>(domain)];
	}

	SimulationFrame SimulationClock::Advance(double realDelta)
	{
		SimulationFrame frame;
		frame.FixedDelta = fixedDelta;
		frame.Frame = ++frameCount;
		if (!std::isfinite(realDelta) || realDelta < 0.0)
		{
			realDelta = 0.0;
		}
		frame.RealDelta = std::min(realDelta, maxFrameDelta);
		real += frame.RealDelta;

		if (paused)
		{
			frame.Paused = true;
			if (pendingSteps > 0)
			{
				--pendingSteps;
				frame.Stepped = true;
				frame.FixedSteps = 1;
				frame.ScaledDelta = fixedDelta;
				frame.Alpha = 1.0;
				simulated += fixedDelta;
				++fixedStepCount;
			}
			else
			{
				frame.Alpha = std::clamp(accumulator / fixedDelta, 0.0, 1.0);
			}
			return frame;
		}

		frame.ScaledDelta = frame.RealDelta * timeScale;
		accumulator += frame.ScaledDelta;
		std::uint32_t steps = static_cast<std::uint32_t>(std::floor(accumulator / fixedDelta));
		if (steps > maxFixedSteps)
		{
			// Keep the simulation responsive: drop what the frame cannot catch up on.
			const double excess = accumulator - double(maxFixedSteps) * fixedDelta;
			dropped += excess;
			accumulator -= excess;
			steps = maxFixedSteps;
		}
		accumulator -= double(steps) * fixedDelta;
		accumulator = std::max(accumulator, 0.0);
		frame.FixedSteps = steps;
		frame.Alpha = std::clamp(accumulator / fixedDelta, 0.0, 1.0);
		fixedStepCount += steps;
		simulated += frame.ScaledDelta;
		return frame;
	}

	double SimulationClock::GetDelta(SimulationDomain domain, const SimulationFrame& frame) const
	{
		return FollowsSimulation(domain) ? frame.ScaledDelta : frame.RealDelta;
	}
} // namespace Engine
