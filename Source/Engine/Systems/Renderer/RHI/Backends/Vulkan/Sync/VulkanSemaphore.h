#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>

namespace Swim::RhiVulkan
{

		class VulkanSemaphore final : public Rhi::Semaphore
		{
		public:
			VulkanSemaphore(std::shared_ptr<VulkanDeviceState> state, VkSemaphore semaphore)
				: state(std::move(state)), semaphore(semaphore)
			{
			}

			~VulkanSemaphore() override
			{
				if (semaphore != VK_NULL_HANDLE)
				{
					state->Dispatch.vkDestroySemaphore(state->Device.device, semaphore, nullptr);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(semaphore);
			}

			VkSemaphore GetSemaphore() const
			{
				return semaphore;
			}

			const std::shared_ptr<VulkanDeviceState>& GetState() const
			{
				return state;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkSemaphore semaphore = VK_NULL_HANDLE;
		};

} // namespace Swim::RhiVulkan
