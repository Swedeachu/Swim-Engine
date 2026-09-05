#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Platform/Internal/WindowInternal.h"

#include <SDL3/SDL_vulkan.h>

#include <type_traits>

namespace Swim::Platform::Internal
{

	namespace
	{

		template <typename Handle>
		Handle FromOpaqueHandle(std::uintptr_t handle)
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

		template <typename Handle>
		std::uintptr_t ToOpaqueHandle(Handle handle)
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

	}

	bool AcquireVulkanLoader()
	{
		return SDL_Vulkan_LoadLibrary(nullptr);
	}

	void ReleaseVulkanLoader()
	{
		SDL_Vulkan_UnloadLibrary();
	}

	VulkanProcAddress GetVulkanInstanceProcAddress()
	{
		return reinterpret_cast<VulkanProcAddress>(SDL_Vulkan_GetVkGetInstanceProcAddr());
	}

	std::span<const char* const> GetVulkanInstanceExtensions()
	{
		Uint32 count = 0;
		const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
		if (!extensions || count == 0)
		{
			return {};
		}
		return { extensions, static_cast<std::size_t>(count) };
	}

	bool GetVulkanPresentationSupport(
		std::uintptr_t instance,
		std::uintptr_t physicalDevice,
		std::uint32_t queueFamilyIndex)
	{
		return SDL_Vulkan_GetPresentationSupport(
			FromOpaqueHandle<VkInstance>(instance),
			FromOpaqueHandle<VkPhysicalDevice>(physicalDevice),
			queueFamilyIndex);
	}

	bool CreateVulkanSurface(
		Window& window,
		std::uintptr_t instance,
		std::uintptr_t& surface)
	{
		SDL_Window* sdlWindow = WindowAccess::GetSdlWindow(window);
		if (!sdlWindow)
		{
			return false;
		}

		// SDL_vulkan.h forward-declares the Vulkan handle types but not
		// VK_NULL_HANDLE, and pulling vulkan.h in here would put the Vulkan API
		// back inside the platform layer. Value-initialization is the null handle
		// for both the pointer and the 64-bit non-dispatchable representations.
		VkSurfaceKHR vkSurface{};
		if (!SDL_Vulkan_CreateSurface(
			sdlWindow,
			FromOpaqueHandle<VkInstance>(instance),
			nullptr,
			&vkSurface))
		{
			return false;
		}

		surface = ToOpaqueHandle(vkSurface);
		return true;
	}

	void DestroyVulkanSurface(std::uintptr_t instance, std::uintptr_t surface)
	{
		if (surface == 0)
		{
			return;
		}

		SDL_Vulkan_DestroySurface(
			FromOpaqueHandle<VkInstance>(instance),
			FromOpaqueHandle<VkSurfaceKHR>(surface),
			nullptr);
	}

}
