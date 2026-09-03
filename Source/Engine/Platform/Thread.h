#pragma once

#include <cstdint>
#include <string_view>

namespace Swim::Platform
{

	bool SetCurrentThreadName(std::string_view name);
	bool SetCurrentThreadAffinity(uint64_t affinityMask);

}
