#pragma once
#include <cstdint>

namespace Swim::Render
{
	// A byte range inside one GeometryHeap page.
	struct GeometryRange
	{
		std::uint64_t Offset = 0;
		std::uint64_t Size = 0;

		bool operator==(const GeometryRange&) const = default;
	};
} // namespace Swim::Render
