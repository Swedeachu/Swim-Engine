#include "Engine/Systems/Renderer/Visibility/VisibilityBinLayout.h"

#include <stdexcept>

namespace Swim::Render
{
	VisibilityBinLayout::VisibilityBinLayout(std::span<const std::uint32_t> materialBinCapacities, std::uint32_t pageSlots)
		: materialBins(static_cast<std::uint32_t>(materialBinCapacities.size())), pageSlots(pageSlots)
	{
		if (materialBinCapacities.empty() || pageSlots == 0)
		{
			throw std::invalid_argument("Visibility bins need at least one material bin and one index-page slot");
		}
		for (const auto capacity : materialBinCapacities)
		{
			for (std::uint32_t slot = 0; slot < pageSlots; ++slot)
			{
				if (capacity > UINT32_MAX - totalCapacity)
				{
					throw std::length_error("Visibility bin capacities overflow");
				}
				ranges.push_back({ totalCapacity, capacity });
				totalCapacity += capacity;
			}
		}
		if (totalCapacity == 0)
		{
			throw std::invalid_argument("Visibility bins need a nonzero total capacity");
		}
	}
} // namespace Swim::Render
