#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanComputePipeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanGraphicsPipeline.h"

namespace Swim::RhiVulkan
{

	void VulkanCommandList::RequireComputeQueue() const
	{
		const auto family = poolState->FamilyIndex;
		if (family >= GetState()->QueueProperties.size() || GetState()->QueueProperties[family].queueCount == 0 ||
			(GetState()->QueueProperties[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
		{
			throw std::logic_error("Compute commands require a compute-capable queue family");
		}
	}

	const VulkanPipelineLayoutState& VulkanCommandList::RequireActivePipeline() const
	{
		if (computePipeline)
		{
			RequireComputeQueue();
			return *computePipeline->GetLayoutState();
		}
		RequireGraphicsQueue();
		if (!graphicsPipeline)
		{
			throw std::logic_error("Bind a pipeline before descriptors or push constants");
		}
		return *graphicsPipeline->GetLayoutState();
	}

	void VulkanCommandList::BindComputePipeline(Rhi::ComputePipeline& pipeline)
	{
		RequireRecording(true);
		RequireComputeQueue();
		auto& native = RequireResource<VulkanComputePipeline>(pipeline, GetState());
		if (native.GetNativeHandle() == 0)
		{
			throw std::invalid_argument("Cannot bind a null compute pipeline");
		}
		boundTables.assign(native.GetLayoutState()->Sets.size(), nullptr);
		GetState()->Dispatch.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			FromNativeHandle<VkPipeline>(native.GetNativeHandle()));
		computePipeline = &native;
		graphicsPipeline = nullptr;
	}

	void VulkanCommandList::Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
	{
		RequireRecording(true);
		RequireComputeQueue();
		if (!computePipeline)
		{
			throw std::logic_error("Dispatch requires an active compute pipeline");
		}
		const auto& limits = GetState()->Device.physical_device.properties.limits;
		if (x > limits.maxComputeWorkGroupCount[0] || y > limits.maxComputeWorkGroupCount[1] || z > limits.maxComputeWorkGroupCount[2])
		{
			throw std::invalid_argument("Dispatch group count exceeds the device limit");
		}
		RequireDescriptorTables();
		RequirePushConstants();
		GetState()->Dispatch.vkCmdDispatch(commandBuffer, x, y, z);
	}

} // namespace Swim::RhiVulkan
