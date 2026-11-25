#pragma once

#include <cstdint>

// Integer hashes for deterministic procedural values on the CPU. PcgHash matches the
// shaders' PCG hash (FeatureCommon.slang FeatureHash, ParticleRecords.slang), so CPU
// mirrors of GPU effects produce the same numbers.
namespace Engine
{

	// PCG hash (Jarzynski and Olano 2020).
	inline std::uint32_t PcgHash(std::uint32_t value)
	{
		const std::uint32_t state = value * 747796405u + 2891336453u;
		const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

} // namespace Engine
