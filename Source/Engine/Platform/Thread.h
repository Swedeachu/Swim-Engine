#pragma once

#include <cstdint>
#include <string_view>

namespace Swim::Platform
{

	bool SetCurrentThreadName(std::string_view name);
	bool SetCurrentThreadAffinity(uint64_t affinityMask);

	// Sleeps for the duration with sub-millisecond precision (frame limiters).
	void SleepPrecise(uint64_t nanoseconds);

}
