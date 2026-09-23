#pragma once
#include "Engine/Systems/Renderer/Visibility/VisibilityBinRange.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Swim::Render
{
	// Bounded binning: every (material bin, index-page slot) pair owns a fixed
	// slice of the command buffer, so one DrawIndexedIndirectCount per bin draws
	// it with a single pipeline and index buffer. Draws beyond a bin's capacity
	// are dropped and counted (VisibilityStats::Dropped), never written elsewhere.
	class VisibilityBinLayout
	{
	  public:
		VisibilityBinLayout() = default;
		// capacities[materialBin] draws per page slot; pageSlots >= 1.
		VisibilityBinLayout(std::span<const std::uint32_t> materialBinCapacities, std::uint32_t pageSlots);

		std::uint32_t GetMaterialBins() const { return materialBins; }

		std::uint32_t GetPageSlots() const { return pageSlots; }

		std::uint32_t GetBinCount() const { return static_cast<std::uint32_t>(ranges.size()); }

		std::uint32_t GetTotalCapacity() const { return totalCapacity; }

		std::uint32_t GetBin(std::uint32_t materialBin, std::uint32_t pageSlot) const { return materialBin * pageSlots + pageSlot; }

		const VisibilityBinRange& GetRange(std::uint32_t bin) const { return ranges.at(bin); }

		const std::vector<VisibilityBinRange>& GetRanges() const { return ranges; }

	  private:
		std::vector<VisibilityBinRange> ranges;
		std::uint32_t materialBins = 0;
		std::uint32_t pageSlots = 0;
		std::uint32_t totalCapacity = 0;
	};
} // namespace Swim::Render
