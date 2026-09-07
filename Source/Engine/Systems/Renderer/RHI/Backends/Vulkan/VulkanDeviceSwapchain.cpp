#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSwapchainColor.h"

namespace Swim::RhiVulkan
{
	namespace
	{
		class WindowSurface
		{
		public:
			WindowSurface(const VulkanDeviceState& state, Platform::Window& window)
				: instance(ToNativeHandle(state.Instance->Instance.instance))
			{
				RequireVulkanDevice(state);
				if (!Platform::Internal::CreateVulkanSurface(window, instance, handle))
				{
					throw std::runtime_error("Failed to create Vulkan window surface");
				}
			}

			~WindowSurface()
			{
				if (handle != 0)
				{
					Platform::Internal::DestroyVulkanSurface(instance, handle);
				}
			}

			WindowSurface(const WindowSurface&) = delete;
			WindowSurface& operator=(const WindowSurface&) = delete;
			VkSurfaceKHR Get() const { return FromNativeHandle<VkSurfaceKHR>(handle); }
			void Release() { handle = 0; }

		private:
			std::uintptr_t instance = 0;
			std::uintptr_t handle = 0;
		};
	}

	Rhi::SwapchainSupport VulkanDevice::QuerySwapchainSupport(Platform::Window& window) const
	{
		WindowSurface surface(*state, window);
		return QueryVulkanSwapchainSupport(*state, surface.Get());
	}

	std::unique_ptr<Rhi::Swapchain> VulkanDevice::CreateSwapchain(
		Platform::Window& window, const Rhi::SwapchainDesc& desc)
	{
		WindowSurface surface(*state, window);
		// Validate even dormant creation, including strict HDR and presentation support.
		if (!SelectSwapchainFormat(QueryVulkanSwapchainSupport(*state, surface.Get()), desc))
		{
			return nullptr;
		}
		auto result = std::make_unique<VulkanSwapchain>(state, window, surface.Get(), desc);
		surface.Release();
		if (!result->Initialize())
		{
			return nullptr;
		}
		return result;
	}

} // namespace Swim::RhiVulkan
