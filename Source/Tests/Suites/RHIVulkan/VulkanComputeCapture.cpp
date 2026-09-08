#include "Tests/Fixtures/VulkanComputeCapture.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanComputeCapture* capture = nullptr;
	}

	VulkanComputeCapture::VulkanComputeCapture()
	{
		capture = this;
		State->QueueProperties.resize(3);
		State->QueueProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
		State->QueueProperties[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
		State->QueueProperties[2].queueFlags = VK_QUEUE_TRANSFER_BIT;
		auto& limits = State->Device.physical_device.properties.limits;
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			State->QueueProperties[axis].queueCount = 1;
			limits.maxComputeWorkGroupSize[axis] = 64;
			limits.maxComputeWorkGroupCount[axis] = 64 - static_cast<std::uint32_t>(axis);
		}
		limits.maxComputeWorkGroupInvocations = 128;
		State->Dispatch.vkCreateComputePipelines = +[](VkDevice, VkPipelineCache cache, std::uint32_t,
			const VkComputePipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* pipeline) -> VkResult
		{
			capture->LastPipelineCache = cache;
			capture->ComputeStage = info->stage.stage;
			capture->ComputeModule = info->stage.module;
			capture->ComputeEntry = info->stage.pName;
			capture->ComputeLayout = info->layout;
			*pipeline = RhiVulkan::FromNativeHandle<VkPipeline>(++capture->PipelinesCreated);
			return capture->PipelineResult;
		};
		State->Dispatch.vkCmdBindPipeline = +[](VkCommandBuffer, VkPipelineBindPoint point, VkPipeline)
		{
			++capture->BindCount;
			capture->BindPoint = point;
		};
		State->Dispatch.vkCmdDispatch = +[](VkCommandBuffer, std::uint32_t x, std::uint32_t y, std::uint32_t z)
		{
			capture->Dispatches.push_back({ x, y, z });
		};
	}

	std::unique_ptr<RhiVulkan::VulkanShaderProgram> VulkanComputeCapture::MakeComputeProgram(
		Rhi::ShaderProgramInterfaceDesc interface, std::array<std::uint32_t, 3> local)
	{
		const std::array<std::uint32_t, 5> header{ 0x07230203, 0x00010500, 0, 1, 0 };
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", std::as_bytes(std::span(header)) };
		interface.ComputeThreadGroupSize = local;
		return RhiVulkan::VulkanShaderProgram::Create(State, { { &stage, 1 }, interface, {} });
	}

	std::unique_ptr<RhiVulkan::VulkanComputePipeline> VulkanComputeCapture::MakeComputePipeline(Rhi::ShaderProgramInterfaceDesc interface)
	{
		auto program = MakeComputeProgram(interface);
		auto layout = RhiVulkan::VulkanPipelineLayout::Create(State, { program.get(), {} });
		return RhiVulkan::VulkanComputePipeline::Create(State, { program.get(), layout.get(), {}, {} });
	}

} // namespace Swim::Testing
