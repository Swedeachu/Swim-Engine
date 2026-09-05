#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <stdexcept>

namespace Swim::RhiVulkan
{

		struct VulkanTimelineState
		{
			std::shared_ptr<VulkanDeviceState> DeviceState;
			VkSemaphore Semaphore = VK_NULL_HANDLE;

			~VulkanTimelineState()
			{
				if (Semaphore != VK_NULL_HANDLE)
				{
					DeviceState->Dispatch.vkDestroySemaphore(DeviceState->Device.device, Semaphore, nullptr);
				}
			}
		};

		class VulkanTimeline final : public Rhi::Timeline
		{
		public:
			explicit VulkanTimeline(std::shared_ptr<VulkanTimelineState> state)
				: state(std::move(state))
			{
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(state->Semaphore);
			}

			std::uint64_t GetCompletedValue() const override
			{
				std::uint64_t value = 0;
				if (state->DeviceState->Dispatch.vkGetSemaphoreCounterValue(
					state->DeviceState->Device.device, state->Semaphore, &value) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to query Vulkan timeline semaphore value");
				}
				return value;
			}

			bool Wait(std::uint64_t value, std::uint64_t timeoutNanoseconds) override
			{
				VkSemaphoreWaitInfo waitInfo{};
				waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
				waitInfo.semaphoreCount = 1;
				waitInfo.pSemaphores = &state->Semaphore;
				waitInfo.pValues = &value;

				const VkResult result = state->DeviceState->Dispatch.vkWaitSemaphores(
					state->DeviceState->Device.device, &waitInfo, timeoutNanoseconds);
				return result == VK_SUCCESS;
			}

			const std::shared_ptr<VulkanTimelineState>& GetState() const
			{
				return state;
			}

		private:
			std::shared_ptr<VulkanTimelineState> state;
		};

} // namespace Swim::RhiVulkan
