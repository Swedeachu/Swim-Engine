#pragma once
#include <cstdint>
#include <vector>

namespace Swim::Render::Internal
{
	struct GraphHazard
	{
		std::uint32_t Writer = UINT32_MAX;
		std::vector<std::uint32_t> Readers;
	};
} // namespace Swim::Render::Internal
