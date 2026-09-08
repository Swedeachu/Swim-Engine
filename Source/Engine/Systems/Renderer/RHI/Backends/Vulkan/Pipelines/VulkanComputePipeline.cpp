#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanComputePipeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineCache.h"

namespace Swim::RhiVulkan
{

	VulkanComputePipeline::VulkanComputePipeline(std::shared_ptr<VulkanDeviceState> state, std::shared_ptr<VulkanPipelineLayoutState> layout)
		: state(std::move(state)), layoutState(std::move(layout))
	{
	}

	VulkanComputePipeline::~VulkanComputePipeline()
	{
		RetireLostVulkanDevice(*state);
		if (pipeline != VK_NULL_HANDLE)
		{
			state->Dispatch.vkDestroyPipeline(state->Device.device, pipeline, nullptr);
		}
	}

	std::unique_ptr<VulkanComputePipeline> VulkanComputePipeline::Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::ComputePipelineDesc& desc)
	{
		if (!state)
		{
			return nullptr;
		}
		RequireVulkanDevice(*state);
		auto* program = dynamic_cast<VulkanShaderProgram*>(desc.Program);
		auto* layout = dynamic_cast<VulkanPipelineLayout*>(desc.Layout);
		if (!program || !layout || program->GetState() != state || layout->GetState() != state ||
			&layout->GetProgram() != program || program->GetStages().size() != 1 ||
			program->GetStages()[0].Stage != VK_SHADER_STAGE_COMPUTE_BIT)
		{
			return nullptr;
		}
		const auto& stage = program->GetStages()[0];
		if (!desc.EntryPoint.empty() && desc.EntryPoint != stage.EntryPoint)
		{
			return nullptr;
		}
		const auto& limits = state->Device.physical_device.properties.limits;
		const auto& local = program->GetInterface().ComputeThreadGroupSize;
		std::uint32_t invocations = 1;
		for (std::size_t axis = 0; axis < local.size(); ++axis)
		{
			if (local[axis] == 0 || local[axis] > limits.maxComputeWorkGroupSize[axis] ||
				invocations > limits.maxComputeWorkGroupInvocations / local[axis])
			{
				return nullptr;
			}
			invocations *= local[axis];
		}
		VkComputePipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		info.stage.module = stage.Module;
		info.stage.pName = stage.EntryPoint.c_str();
		info.layout = layout->GetLayoutState()->Layout;
		auto result = std::make_unique<VulkanComputePipeline>(state, layout->GetLayoutState());
		if (CreateCachedVulkanComputePipeline(*state, info, result->pipeline) != VK_SUCCESS)
		{
			return nullptr; // RAII also releases a partial native pipeline on failure.
		}
		SetVulkanObjectName(*state, VK_OBJECT_TYPE_PIPELINE, ToNativeHandle(result->pipeline), desc.DebugName);
		return result;
	}

	std::uintptr_t VulkanComputePipeline::GetNativeHandle() const
	{
		return ToNativeHandle(pipeline);
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanComputePipeline::GetState() const
	{
		return state;
	}

	const std::shared_ptr<VulkanPipelineLayoutState>& VulkanComputePipeline::GetLayoutState() const
	{
		return layoutState;
	}

} // namespace Swim::RhiVulkan
