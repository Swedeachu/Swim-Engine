#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Push constants of SwimTemporalResolve (Shaders/Slang/Temporal/TemporalRecords.slang).
	struct TemporalResolveConstants
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		float Feedback = 0.1f;
		float ClipGamma = 1.25f;
		std::uint32_t HistoryValid = 0; // 0: the output is the current frame.
		std::uint32_t Reserved[3] = {};
	};

	static_assert(sizeof(TemporalResolveConstants) == 32);
} // namespace Swim::Render
