#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Queries/VulkanQueryPool.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanFence.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanSemaphore.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanTimeline.h"

#include <unordered_set>
#include <stdexcept>
#include <vector>

namespace Swim::RhiVulkan
{

	Rhi::TimestampInfo VulkanQueue::GetTimestampInfo() const
	{
		return GetVulkanTimestampInfo(*state, familyIndex);
	}

		void VulkanQueue::Submit(const Rhi::SubmitDesc& desc)
		{
			RequireVulkanDevice(*state);
			std::vector<VkCommandBufferSubmitInfo> commandInfos;
			commandInfos.reserve(desc.CommandLists.size());
			std::unordered_set<VkCommandBuffer> submittedCommands;
			submittedCommands.reserve(desc.CommandLists.size());

			for (Rhi::CommandList* commandList : desc.CommandLists)
			{
				auto* vulkanCommandList = dynamic_cast<VulkanCommandList*>(commandList);

				if (vulkanCommandList == nullptr || vulkanCommandList->GetState().get() != state.get() ||
					vulkanCommandList->GetQueueFamilyIndex() != familyIndex || !vulkanCommandList->IsExecutable())
				{
					throw std::invalid_argument("Vulkan queue submission requires executable, unsubmitted, same-device command lists from the matching queue family");
				}

				if (!submittedCommands.insert(vulkanCommandList->GetCommandBuffer()).second)
				{
					throw std::invalid_argument("Vulkan one-time command lists cannot occur twice in one submission");
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

			std::unique_lock progressLock(progressMutex, std::defer_lock);

			if (progressTimeline != VK_NULL_HANDLE)
			{
				progressLock.lock();
				VkSemaphoreSubmitInfo progressInfo{};
				progressInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				progressInfo.semaphore = progressTimeline;
				progressInfo.value = state->Deferred->Next(progressIndex);
				progressInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signalInfos.push_back(progressInfo);
			}

			auto submit = [state = state, queue = queue, mutex = submissionMutex, commandInfos = std::move(commandInfos),
							  waitInfos = std::move(waitInfos), signalInfos = std::move(signalInfos), completionFence]
			{
				VkSubmitInfo2 submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
				submitInfo.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waitInfos.size());
				submitInfo.pWaitSemaphoreInfos = waitInfos.data();
				submitInfo.commandBufferInfoCount = static_cast<std::uint32_t>(commandInfos.size());
				submitInfo.pCommandBufferInfos = commandInfos.data();
				submitInfo.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos.size());
				submitInfo.pSignalSemaphoreInfos = signalInfos.data();
				std::scoped_lock lock(*mutex);
				RequireVulkanDevice(*state);

				if (CheckVulkanResult(*state, state->Dispatch.vkQueueSubmit2(queue, 1, &submitInfo, completionFence), "vkQueueSubmit2") != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to submit work to Vulkan queue");
				}
			};

			// On the submission thread the work is queued in order and the caller goes on; a
			// failure surfaces at the next submission or wait.
			if (state->SubmitWorker && !state->SubmitWorker->IsWorkerThread())
			{
				state->SubmitWorker->Post(std::move(submit));
			}
			else
			{
				submit();
			}

			if (progressLock.owns_lock())
			{
				progressLock.unlock();
			}

			for (Rhi::CommandList* commandList : desc.CommandLists)
			{
				static_cast<VulkanCommandList*>(commandList)->MarkSubmitted();
			}

			state->Deferred->Collect();
		}

		void VulkanQueue::WaitIdle()
		{
			RequireVulkanDevice(*state);
			state->DrainSubmissions();
			std::scoped_lock lock(*submissionMutex);
			RequireVulkanDevice(*state);

			if (CheckVulkanResult(*state, state->Dispatch.vkQueueWaitIdle(queue), "vkQueueWaitIdle") != VK_SUCCESS)
			{
				throw std::runtime_error("Failed waiting for Vulkan queue idle");
			}

			// Presents issued through the graphics queue run on a presentation queue of its own
			// when the device has one; idle covers them too, so their wait semaphores are free.
			if (type == Rhi::QueueType::Graphics && state->PresentationQueue != VK_NULL_HANDLE && state->PresentationQueueMutex)
			{
				std::scoped_lock presentLock(*state->PresentationQueueMutex);

				if (CheckVulkanResult(
						*state, state->Dispatch.vkQueueWaitIdle(state->PresentationQueue), "vkQueueWaitIdle (presentation)") != VK_SUCCESS)
				{
					throw std::runtime_error("Failed waiting for Vulkan presentation queue idle");
				}
			}

			if (progressTimeline != VK_NULL_HANDLE)
			{
				state->Deferred->Idle(progressIndex);
			}
		}

		void VulkanQueue::EnableProgress()
		{
			if (progressTimeline != VK_NULL_HANDLE)
			{
				return;
			}

			VkSemaphoreTypeCreateInfo typeInfo{};
			typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
			typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
			VkSemaphoreCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			createInfo.pNext = &typeInfo;
			VkSemaphore timeline = VK_NULL_HANDLE;

			if (CheckVulkanResult(*state, state->Dispatch.vkCreateSemaphore(state->Device.device, &createInfo, nullptr, &timeline),
					"vkCreateSemaphore (queue progress)") != VK_SUCCESS)
			{
				return; // Releases stay immediate for this queue's work.
			}

			SetVulkanObjectName(*state, VK_OBJECT_TYPE_SEMAPHORE, ToNativeHandle(timeline), "Swim queue progress");
			progressIndex = state->Deferred->Register(state->Device.device, state->Dispatch.vkGetSemaphoreCounterValue,
				state->Dispatch.vkDestroySemaphore, timeline, type == Rhi::QueueType::Graphics);
			progressTimeline = timeline;
		}

} // namespace Swim::RhiVulkan
