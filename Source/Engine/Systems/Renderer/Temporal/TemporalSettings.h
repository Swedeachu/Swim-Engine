#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Temporal anti-aliasing (critical-path item 75). TemporalReference.h defines each value.
	struct TemporalSettings
	{
		// Weight of the current frame once history is valid, (0, 1]. Lower is smoother
		// and slower to respond; 1 disables accumulation.
		float Feedback = 0.1f;
		// Half-size of the variance clipping box in standard deviations, [0.25, 8]. Smaller
		// rejects more history (less ghosting, more flicker).
		float ClipGamma = 1.25f;
		// Length of the Halton(2, 3) jitter sequence, 0 .. MaxJitterPhases. 0 renders
		// without jitter (history still smooths motion, no sub-pixel coverage).
		std::uint32_t JitterPhases = 8;
	};

	inline constexpr std::uint32_t MaxJitterPhases = 64;

	// Throws std::invalid_argument for values outside the ranges above.
	void ValidateTemporalSettings(const TemporalSettings& settings);
} // namespace Swim::Render
