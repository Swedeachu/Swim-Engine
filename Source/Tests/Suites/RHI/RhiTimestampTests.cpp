#include "Engine/Systems/Renderer/RHI/RhiTimestamps.h"
#include "Tests/Framework/Test.h"

#include <limits>

using namespace Swim;

SWIM_TEST("RHI.Timestamps", "FractionalPeriodAndCounterWrapPreserveElapsedTime")
{
	Rhi::TimestampInfo info{ 0.25, 8 };
	SWIM_CHECK(info.IsSupported());
	SWIM_CHECK(info.ElapsedNanoseconds({ 250, true }, { 10, true }) == 4.0);
	SWIM_CHECK(info.ElapsedNanoseconds({ 0x1fa, true }, { 0x20a, true }) == 4.0);
	SWIM_CHECK(info.ElapsedNanoseconds({ 123, true }, { 123, true }) == 0.0);
	info.ValidBits = 64;
	SWIM_CHECK(info.ElapsedNanoseconds({ UINT64_MAX - 3, true }, { 4, true }) == 2.0);
	// Subtract integer ticks before floating conversion: absolute values above
	// double's exact integer range must not erase a short measured interval.
	SWIM_CHECK(info.ElapsedNanoseconds({ UINT64_MAX - 2, true }, { UINT64_MAX, true }) == 0.5);
}

SWIM_TEST("RHI.Timestamps", "UnavailableAndInvalidClockMetadataHaveNoDuration")
{
	Rhi::TimestampInfo info{ 1.0, 64 };
	SWIM_CHECK(!info.ElapsedNanoseconds({ 1, false }, { 2, true }));
	SWIM_CHECK(!info.ElapsedNanoseconds({ 1, true }, { 2, false }));
	for (auto bits : { 0u, 65u, UINT32_MAX })
	{
		info.ValidBits = bits;
		SWIM_CHECK(!info.IsSupported());
		SWIM_CHECK(!info.ElapsedNanoseconds({ 1, true }, { 2, true }));
	}
	info.ValidBits = 64;
	for (auto period : { 0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
	{
		info.NanosecondsPerTick = period;
		SWIM_CHECK(!info.IsSupported());
		SWIM_CHECK(!info.ElapsedNanoseconds({ 1, true }, { 2, true }));
	}
	info.NanosecondsPerTick = std::numeric_limits<double>::max();
	SWIM_CHECK(!info.ElapsedNanoseconds({ 0, true }, { 2, true }));
}
