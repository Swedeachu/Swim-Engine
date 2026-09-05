#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <stdexcept>

namespace Swim::RhiVulkan
{

		class VulkanFence final : public Rhi::Fence
		{
		public:
			VulkanFence(std::shared_ptr<VulkanDeviceState> state, VkFence fence)
				: state(std::move(state)), fence(fence)
			{
			}

			~VulkanFence() override
			{
				if (fence != VK_NULL_HANDLE)
				{
					state->Dispatch.vkDestroyFence(state->Device.device, fence, nullptr);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(fence);
			}

			bool IsSignaled() const override
			{
				return state->Dispatch.vkGetFenceStatus(state->Device.device, fence) == VK_SUCCESS;
			}

			bool Wait(std::uint64_t timeoutNanoseconds) override
			{
				const VkResult result = state->Dispatch.vkWaitForFences(
					state->Device.device, 1, &fence, VK_TRUE, timeoutNanoseconds);
				return result == VK_SUCCESS;
			}

			void Reset() override
			{
				if (state->Dispatch.vkResetFences(state->Device.device, 1, &fence) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to reset Vulkan fence");
				}
			}

			VkFence GetFence() const
			{
				return fence;
			}

			const std::shared_ptr<VulkanDeviceState>& GetState() const
			{
				return state;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkFence fence = VK_NULL_HANDLE;
		};

} // namespace Swim::RhiVulkan
