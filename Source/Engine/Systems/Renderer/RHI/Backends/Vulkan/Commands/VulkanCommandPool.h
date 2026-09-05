#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandPoolState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <stdexcept>

namespace Swim::RhiVulkan
{

		class VulkanCommandPool final : public Rhi::CommandPool
		{
		public:
			explicit VulkanCommandPool(std::shared_ptr<VulkanCommandPoolState> state)
				: state(std::move(state))
			{
				SetVulkanObjectName(*this->state->DeviceState, VK_OBJECT_TYPE_COMMAND_POOL,
					ToNativeHandle(this->state->Pool), "Swim command pool");
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(state->Pool);
			}

			std::unique_ptr<Rhi::CommandList> CreateCommandList() override
			{
				VkCommandBufferAllocateInfo allocateInfo{};
				allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocateInfo.commandPool = state->Pool;
				allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocateInfo.commandBufferCount = 1;

				VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
				if (state->DeviceState->Dispatch.vkAllocateCommandBuffers(
					state->DeviceState->Device.device, &allocateInfo, &commandBuffer) != VK_SUCCESS)
				{
					return nullptr;
				}
				return std::make_unique<VulkanCommandList>(state, commandBuffer);
			}

			void Reset() override
			{
				if (state->DeviceState->Dispatch.vkResetCommandPool(
					state->DeviceState->Device.device, state->Pool, 0) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to reset Vulkan command pool");
				}
				++state->Generation;
			}

		private:
			std::shared_ptr<VulkanCommandPoolState> state;
		};

} // namespace Swim::RhiVulkan
