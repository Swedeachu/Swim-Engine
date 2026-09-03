#pragma once

#include <chrono>

namespace Swim::Platform
{

	class MonotonicClock
	{
	public:

		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;

		static TimePoint Now() { return Clock::now(); }
		static double SecondsBetween(TimePoint start, TimePoint end)
		{
			return std::chrono::duration<double>(end - start).count();
		}
	};

}
