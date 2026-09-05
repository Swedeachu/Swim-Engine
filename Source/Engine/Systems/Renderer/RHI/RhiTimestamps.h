#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace Swim::Rhi
{

	enum class TimestampStage : std::uint8_t
	{
		Begin, // Before the measured commands enter the pipeline.
		End, // After earlier commands have completed all pipeline stages.
	};

	enum class QueryReadStatus : std::uint8_t
	{
		Ready,
		NotReady,
		Error,
	};

	struct TimestampResult
	{
		std::uint64_t Ticks = 0;
		bool Available = false;
	};

	struct TimestampInfo
	{
		double NanosecondsPerTick = 0.0;
		std::uint32_t ValidBits = 0;

		bool IsSupported() const
		{
			return ValidBits > 0 && ValidBits <= 64 &&
				std::isfinite(NanosecondsPerTick) && NanosecondsPerTick > 0.0;
		}

		// Compare available results from one queue, ordered within one submission.
		// The measured interval must be shorter than one complete counter wrap.
		// Timestamps are GPU timings, not calibrated CPU or cross-queue clocks.
		std::optional<double> ElapsedNanoseconds(TimestampResult begin, TimestampResult end) const
		{
			if (!IsSupported() || !begin.Available || !end.Available)
			{
				return std::nullopt;
			}
			const std::uint64_t mask = ValidBits == 64 ? UINT64_MAX : (std::uint64_t{ 1 } << ValidBits) - 1;
			const double elapsed = static_cast<double>((end.Ticks - begin.Ticks) & mask) * NanosecondsPerTick;
			if (!std::isfinite(elapsed))
			{
				return std::nullopt;
			}
			return elapsed;
		}
	};

} // namespace Swim::Rhi
