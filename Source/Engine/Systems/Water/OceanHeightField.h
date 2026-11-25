#pragma once

#include "Engine/Systems/Physics/PhysicsBuoyancy.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace Engine
{

	// One tiling displacement map on the CPU: Displacement[y * Resolution + x] is the xyz offset
	// (metres, y up) of the rest point at world xz = (texel centre) * TileSize / Resolution.
	struct OceanFieldCascade
	{
		float TileSize = 1.0f;
		std::uint32_t Resolution = 0;
		std::vector<glm::vec3> Displacement;

		// From RGBA16Float texels (tightly packed rows), keeping every Step-th texel.
		static OceanFieldCascade FromHalfFloats(
			std::span<const std::uint16_t> rgba, std::uint32_t resolution, std::uint32_t step, float tileSize);
	};

	// A displaced sea surface (rest point + the sum of tiling displacement cascades) sampled
	// on the CPU: the height under a world column, found by inverting the horizontal
	// (choppy) offset, and the surface's velocity from the last two published fields.
	// Publish from one thread and Sample from any (each sample reads one consistent pair).
	class OceanHeightField
	{

	  public:
		// time: the simulation's seconds for these maps (wrapping every repeatSeconds).
		void Publish(std::vector<OceanFieldCascade> cascades, double time, double repeatSeconds);

		// seaLevel: the mean surface height. now: the simulation's current seconds (heights
		// are moved along the velocity from the published time, at most a quarter second).
		// Flat and still at seaLevel until something has been published.
		FluidSample Sample(float x, float z, float seaLevel, double now) const;

		bool HasData() const;

		void Clear();

	  private:
		struct Snapshot
		{
			std::vector<OceanFieldCascade> Cascades;
			double Time = 0.0;
			double Repeat = 0.0;
		};

		static glm::vec3 Displacement(const Snapshot& snapshot, glm::vec2 rest);

		mutable std::mutex mutex;
		std::shared_ptr<const Snapshot> current;
		std::shared_ptr<const Snapshot> previous;
	};

} // namespace Engine
