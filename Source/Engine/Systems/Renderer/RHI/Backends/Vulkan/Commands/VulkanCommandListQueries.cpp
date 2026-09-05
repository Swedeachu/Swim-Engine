#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Queries/VulkanQueryPool.h"

namespace Swim::RhiVulkan
{

	namespace
	{
		VulkanQueryPool& RequireQueryPool(Rhi::QueryPool& pool, const VulkanDeviceState& state,
			std::uint32_t family, std::uint32_t first, std::uint32_t count)
		{
			auto* queries = dynamic_cast<VulkanQueryPool*>(&pool);
			if (queries == nullptr || !queries->Matches(state, family) || !queries->Contains(first, count) ||
				!GetVulkanTimestampInfo(state, family).IsSupported())
			{
				throw std::invalid_argument("Vulkan query command requires a supported same-device/family timestamp pool and valid range");
			}
			return *queries;
		}
	}

	void VulkanCommandList::ResetQueries(Rhi::QueryPool& pool, std::uint32_t first, std::uint32_t count)
	{
		RequireRecording(true);
		auto& queries = RequireQueryPool(pool, *GetState(), GetQueueFamilyIndex(), first, count);
		GetState()->Dispatch.vkCmdResetQueryPool(commandBuffer,
			FromNativeHandle<VkQueryPool>(queries.GetNativeHandle()), first, count);
	}

	void VulkanCommandList::WriteTimestamp(Rhi::QueryPool& pool, std::uint32_t index, Rhi::TimestampStage stage)
	{
		RequireRecording(true);
		auto& queries = RequireQueryPool(pool, *GetState(), GetQueueFamilyIndex(), index, 1);
		VkPipelineStageFlags2 nativeStage = 0;
		switch (stage)
		{
		case Rhi::TimestampStage::Begin:
			nativeStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			break;
		case Rhi::TimestampStage::End:
			nativeStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			break;
		default:
			throw std::invalid_argument("Unknown RHI timestamp stage");
		}
		GetState()->Dispatch.vkCmdWriteTimestamp2(commandBuffer, nativeStage,
			FromNativeHandle<VkQueryPool>(queries.GetNativeHandle()), index);
	}

} // namespace Swim::RhiVulkan
