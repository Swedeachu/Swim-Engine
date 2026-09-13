#pragma once
#include <cstdint>
#include <limits>

namespace Swim::Render
{
	struct GraphResourceLifetime
	{
		static constexpr std::uint32_t Unused = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t First = Unused;
		std::uint32_t Last = Unused;
		std::uint32_t Allocation = Unused;
	};
} // namespace Swim::Render
