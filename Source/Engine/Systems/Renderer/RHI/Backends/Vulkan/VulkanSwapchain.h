#pragma once

#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Platform/Window.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSwapchainSession.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTexture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>
#include <VkBootstrap.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Swim::RhiVulkan
{

		class VulkanSwapchain final : public Rhi::Swapchain
		{
		public:
			VulkanSwapchain(
				std::shared_ptr<VulkanDeviceState> state,
				Platform::Window& window,
				VkSurfaceKHR surface,
				Rhi::SwapchainDesc desc)
				: state(std::move(state)), window(window), surface(surface), desc(desc), session(this->state)
			{
			}

			~VulkanSwapchain() override;

			bool Initialize();

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(swapchain.swapchain);
			}

			Rhi::Format GetFormat() const override
			{
				return format;
			}

			Rhi::Extent2D GetExtent() const override
			{
				return extent;
			}

			std::uint32_t GetImageCount() const override
			{
				return static_cast<std::uint32_t>(views.size());
			}

			Rhi::TextureView& GetImageView(std::uint32_t imageIndex) override
			{
				return *views.at(imageIndex);
			}

			Rhi::SwapchainAcquireResult AcquireNextImage(
				Rhi::Semaphore& signalSemaphore,
				Rhi::Fence* signalFence) override;

			bool Present(
				Rhi::Queue& queue,
				std::uint32_t imageIndex,
				std::span<Rhi::Semaphore* const> waits) override;

			bool Resize(Rhi::Extent2D requestedExtent, const Rhi::TimelinePoint& safeAfter) override;

		private:
			// Rebuilds the swapchain at the requested size. safeAfter is null only for
			// the very first build (Initialize); every later rebuild is a live
			// replacement and must prove the old images are safe to retire (see
			// Resize/Rebuild in VulkanSwapchain.cpp for the retirement-timeline wait).
			bool Rebuild(std::uint32_t width, std::uint32_t height, const Rhi::TimelinePoint* safeAfter);
			void DestroySwapchain();
			bool WaitForRetirement(const Rhi::TimelinePoint* safeAfter);

			std::shared_ptr<VulkanDeviceState> state;
			Platform::Window& window;
			VkSurfaceKHR surface = VK_NULL_HANDLE;
			Rhi::SwapchainDesc desc{};
			VulkanSwapchainSession session;
			vkb::Swapchain swapchain{};
			std::vector<VkImageView> imageViews;
			std::vector<std::unique_ptr<VulkanTexture>> textures;
			std::vector<std::unique_ptr<VulkanTextureView>> views;
			Rhi::Format format = Rhi::Format::Undefined;
			Rhi::Extent2D extent{};
		};

} // namespace Swim::RhiVulkan
