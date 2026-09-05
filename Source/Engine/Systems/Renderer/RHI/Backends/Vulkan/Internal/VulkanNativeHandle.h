#pragma once

// Generic native-handle conversion helpers shared by every Vulkan RHI
// implementation type. Split out of the former monolithic
// VulkanRhiBackend.cpp so every backend type file can use these without
// depending on the rest of the backend.

#include <cstdint>
#include <type_traits>

namespace Swim::RhiVulkan
{

		template <typename Handle>
		std::uintptr_t ToNativeHandle(Handle handle)
		{
			if constexpr (std::is_pointer_v<Handle>)
			{
				return reinterpret_cast<std::uintptr_t>(handle);
			}
			else
			{
				return static_cast<std::uintptr_t>(handle);
			}
		}

		template <typename Handle>
		Handle FromNativeHandle(std::uintptr_t handle)
		{
			if constexpr (std::is_pointer_v<Handle>)
			{
				return reinterpret_cast<Handle>(handle);
			}
			else
			{
				return static_cast<Handle>(handle);
			}
		}

} // namespace Swim::RhiVulkan
