#pragma once
#include <string>
#include <optional>

namespace Swim::Render
{
	// GPU timestamps of one scheduled pass. The begin timestamp is written at the top of
	// the pipe before the pass's barriers, so Nanoseconds can overlap the tail of earlier
	// work; the end timestamp is written after the pass completes. EndOffsetNanoseconds
	// (end relative to the graph's first begin) is monotonic in schedule order, so the
	// difference between consecutive passes' end offsets attributes GPU time to each pass
	// without overlap.
	struct GraphPassTiming
	{
		std::string Name;
		std::optional<double> Nanoseconds;
		std::optional<double> EndOffsetNanoseconds;
	};
} // namespace Swim::Render
