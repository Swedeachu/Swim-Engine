#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <mutex>

namespace Swim::RhiVulkan
{

		class VulkanQueue final : public Rhi::Queue
		{
		public:
			VulkanQueue(
				std::shared_ptr<VulkanDeviceState> state,
				Rhi::QueueType type,
				VkQueue queue,
				std::uint32_t familyIndex,
				std::shared_ptr<std::mutex> submissionMutex)
				: state(std::move(state)),
				  type(type),
				  queue(queue),
				  familyIndex(familyIndex),
				  submissionMutex(std::move(submissionMutex))
			{
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(queue);
			}

			Rhi::QueueType GetType() const override
			{
				return type;
			}

			const std::shared_ptr<VulkanDeviceState>& GetState() const
			{
				return state;
			}

			std::uint32_t GetFamilyIndex() const
			{
				return familyIndex;
			}

			VkQueue GetQueue() const
			{
				return queue;
			}

			std::mutex& GetMutex() const
			{
				return *submissionMutex;
			}

			Rhi::TimestampInfo GetTimestampInfo() const override;
			void Submit(const Rhi::SubmitDesc& desc) override;
			void WaitIdle() override;

		private:
			std::shared_ptr<VulkanDeviceState> state;
			Rhi::QueueType type = Rhi::QueueType::Graphics;
			VkQueue queue = VK_NULL_HANDLE;
			std::uint32_t familyIndex = 0;
			std::shared_ptr<std::mutex> submissionMutex;
		};

} // namespace Swim::RhiVulkan
