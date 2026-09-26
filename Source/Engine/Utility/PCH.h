#pragma once

// Precompiled header for the SwimEngine executable. Only stable, widely used
// headers belong here: every source must still include what it uses, so the
// build stays correct with SWIM_ENABLE_PCH=OFF and on every platform.

// STL
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Math / ECS
#include <entt/entt.hpp>
#include <glm/glm.hpp>
