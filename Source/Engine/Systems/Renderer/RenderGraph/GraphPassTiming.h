#pragma once
#include <string>
#include <optional>

namespace Swim::Render
{
	struct GraphPassTiming
	{
		std::string Name;
		std::optional<double> Nanoseconds;
	};
} // namespace Swim::Render
