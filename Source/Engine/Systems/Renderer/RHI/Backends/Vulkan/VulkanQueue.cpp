#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanFence.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanSemaphore.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanTimeline.h"

#include <stdexcept>
#include <vector>

namespace Swim::RhiVulkan
{

		void VulkanQueue::Submit(const Rhi::SubmitDesc& desc)
		{
			std::vector<VkCommandBufferSubmitInfo> commandInfos;
			commandInfos.reserve(desc.CommandLists.size());
			for (Rhi::CommandList* commandList : desc.CommandLists)
			{
				auto* vulkanCommandList = dynamic_cast<VulkanCommandList*>(commandList);
				if (vulkanCommandList == nullptr || vulkanCommandList->GetState().get() != state.get() ||
					vulkanCommandList->GetQueueFamilyIndex() != familyIndex)
				{
					throw std::invalid_argument("Vulkan queue submission requires same-device command lists from the matching queue family");
				}

				VkCommandBufferSubmitInfo commandInfo{};
				commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
				commandInfo.commandBuffer = vulkanCommandList->GetCommandBuffer();
				commandInfo.deviceMask = 1;
				commandInfos.push_back(commandInfo);
			}

			std::vector<VkSemaphoreSubmitInfo> waitInfos;
			waitInfos.reserve(desc.WaitSemaphores.size() + desc.WaitTimelines.size());
			for (Rhi::Semaphore* semaphore : desc.WaitSemaphores)
			{
				auto* vulkanSemaphore = dynamic_cast<VulkanSemaphore*>(semaphore);
				if (vulkanSemaphore == nullptr || vulkanSemaphore->GetState().get() != state.get())
				{
					throw std::invalid_argument("Vulkan queue submission requires same-device Vulkan semaphores");
				}

				VkSemaphoreSubmitInfo waitInfo{};
				waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				waitInfo.semaphore = vulkanSemaphore->GetSemaphore();
				waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				waitInfos.push_back(waitInfo);
			}
			for (const Rhi::TimelinePoint& point : desc.WaitTimelines)
			{
				auto* timeline = dynamic_cast<VulkanTimeline*>(point.Semaphore);
				if (timeline == nullptr || timeline->GetState()->DeviceState.get() != state.get())
				{
					throw std::invalid_argument("Vulkan queue timeline waits must belong to the same Vulkan device");
				}

				VkSemaphoreSubmitInfo waitInfo{};
				waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				waitInfo.semaphore = timeline->GetState()->Semaphore;
				waitInfo.value = point.Value;
				waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				waitInfos.push_back(waitInfo);
			}

			std::vector<VkSemaphoreSubmitInfo> signalInfos;
			signalInfos.reserve(desc.SignalSemaphores.size() + desc.SignalTimelines.size());
			for (Rhi::Semaphore* semaphore : desc.SignalSemaphores)
			{
				auto* vulkanSemaphore = dynamic_cast<VulkanSemaphore*>(semaphore);
				if (vulkanSemaphore == nullptr || vulkanSemaphore->GetState().get() != state.get())
				{
					throw std::invalid_argument("Vulkan queue submission requires same-device Vulkan semaphores");
				}

				VkSemaphoreSubmitInfo signalInfo{};
				signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				signalInfo.semaphore = vulkanSemaphore->GetSemaphore();
				signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signalInfos.push_back(signalInfo);
			}
			for (const Rhi::TimelinePoint& point : desc.SignalTimelines)
			{
				auto* timeline = dynamic_cast<VulkanTimeline*>(point.Semaphore);
				if (timeline == nullptr || timeline->GetState()->DeviceState.get() != state.get())
				{
					throw std::invalid_argument("Vulkan queue timeline signals must belong to the same Vulkan device");
				}

				VkSemaphoreSubmitInfo signalInfo{};
				signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				signalInfo.semaphore = timeline->GetState()->Semaphore;
				signalInfo.value = point.Value;
				signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signalInfos.push_back(signalInfo);
			}

			VkFence completionFence = VK_NULL_HANDLE;
			if (desc.CompletionFence != nullptr)
			{
				auto* fence = dynamic_cast<VulkanFence*>(desc.CompletionFence);
				if (fence == nullptr || fence->GetState().get() != state.get())
				{
					throw std::invalid_argument("Vulkan queue submission requires a same-device Vulkan completion fence");
				}
				completionFence = fence->GetFence();
			}

			VkSubmitInfo2 submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
			submitInfo.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waitInfos.size());
			submitInfo.pWaitSemaphoreInfos = waitInfos.data();
			submitInfo.commandBufferInfoCount = static_cast<std::uint32_t>(commandInfos.size());
			submitInfo.pCommandBufferInfos = commandInfos.data();
			submitInfo.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos.size());
			submitInfo.pSignalSemaphoreInfos = signalInfos.data();

			std::scoped_lock lock(*submissionMutex);
			if (state->Dispatch.vkQueueSubmit2(queue, 1, &submitInfo, completionFence) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to submit work to Vulkan queue");
			}
		}

		void VulkanQueue::WaitIdle()
		{
			std::scoped_lock lock(*submissionMutex);
			if (state->Dispatch.vkQueueWaitIdle(queue) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed waiting for Vulkan queue idle");
			}
		}

} // namespace Swim::RhiVulkan
