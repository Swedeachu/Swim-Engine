#pragma once

#include <cstdint>
#include <span>

namespace Swim::Platform
{

	class Window;

	namespace Internal
	{

		using VulkanProcAddress = void (*)();

		bool AcquireVulkanLoader();
		void ReleaseVulkanLoader();
		VulkanProcAddress GetVulkanInstanceProcAddress();
		std::span<const char* const> GetVulkanInstanceExtensions();
		bool GetVulkanPresentationSupport(
			std::uintptr_t instance,
			std::uintptr_t physicalDevice,
			std::uint32_t queueFamilyIndex);
		bool CreateVulkanSurface(
			Window& window,
			std::uintptr_t instance,
			std::uintptr_t& surface);
		void DestroyVulkanSurface(std::uintptr_t instance, std::uintptr_t surface);

	}

}
