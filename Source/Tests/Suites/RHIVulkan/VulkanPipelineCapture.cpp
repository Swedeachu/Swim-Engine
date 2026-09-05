#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanPipelineCapture* capture = nullptr;
	}

	VulkanPipelineCapture::VulkanPipelineCapture()
	{
		capture = this;
		State->Instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
		auto& limits = State->Device.physical_device.properties.limits;
		limits.framebufferColorSampleCounts = limits.framebufferDepthSampleCounts = limits.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
		State->Instance->Dispatch.vkGetPhysicalDeviceFormatProperties = +[](VkPhysicalDevice, VkFormat, VkFormatProperties* properties)
		{
			properties->optimalTilingFeatures = capture->FormatFeatures;
		};
		State->Dispatch.vkCreateShaderModule = +[](VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* module) -> VkResult
		{
			++capture->ModulesCreated;
			if (capture->ModulesCreated == capture->FailModule)
			{
				return VK_ERROR_OUT_OF_HOST_MEMORY;
			}
			*module = RhiVulkan::FromNativeHandle<VkShaderModule>(capture->ModulesCreated);
			return VK_SUCCESS;
		};
		State->Dispatch.vkDestroyShaderModule = +[](VkDevice, VkShaderModule, const VkAllocationCallbacks*) { ++capture->ModulesDestroyed; };
		State->Dispatch.vkCreatePipelineLayout = +[](VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* layout) -> VkResult
		{
			*layout = RhiVulkan::FromNativeHandle<VkPipelineLayout>(++capture->LayoutsCreated);
			return VK_SUCCESS;
		};
		State->Dispatch.vkDestroyPipelineLayout = +[](VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) { ++capture->LayoutsDestroyed; };
		State->Dispatch.vkCreateGraphicsPipelines = +[](VkDevice, VkPipelineCache, std::uint32_t, const VkGraphicsPipelineCreateInfo* info,
			const VkAllocationCallbacks*, VkPipeline* pipeline) -> VkResult
		{
			*pipeline = RhiVulkan::FromNativeHandle<VkPipeline>(++capture->PipelinesCreated);
			const auto& rendering = *static_cast<const VkPipelineRenderingCreateInfo*>(info->pNext);
			capture->PipelineColors.assign(rendering.pColorAttachmentFormats, rendering.pColorAttachmentFormats + rendering.colorAttachmentCount);
			capture->PipelineDepth = rendering.depthAttachmentFormat;
			capture->PipelineStencil = rendering.stencilAttachmentFormat;
			capture->Topology = info->pInputAssemblyState->topology;
			capture->Winding = info->pRasterizationState->frontFace;
			capture->DepthState = *info->pDepthStencilState;
			if (info->pColorBlendState->attachmentCount != 0)
			{
				capture->Blend = info->pColorBlendState->pAttachments[0];
			}
			capture->DynamicStates.assign(info->pDynamicState->pDynamicStates,
				info->pDynamicState->pDynamicStates + info->pDynamicState->dynamicStateCount);
			return capture->PipelineResult;
		};
		State->Dispatch.vkDestroyPipeline = +[](VkDevice, VkPipeline, const VkAllocationCallbacks*) { ++capture->PipelinesDestroyed; };
		State->Dispatch.vkCmdBindPipeline = +[](VkCommandBuffer, VkPipelineBindPoint, VkPipeline) { ++capture->BindCount; };
		State->Dispatch.vkCmdBindIndexBuffer = +[](VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType) {};
		State->Dispatch.vkCmdDraw = +[](VkCommandBuffer, std::uint32_t vertices, std::uint32_t instances, std::uint32_t firstVertex, std::uint32_t firstInstance)
		{
			++capture->DrawCount;
			capture->Draw = { vertices, instances, firstVertex, firstInstance };
		};
		State->Dispatch.vkCmdDrawIndexed = +[](VkCommandBuffer, std::uint32_t indices, std::uint32_t instances,
			std::uint32_t firstIndex, std::int32_t vertexOffset, std::uint32_t firstInstance)
		{
			++capture->IndexedDrawCount;
			capture->IndexedDraw = { indices, instances, firstIndex, vertexOffset, firstInstance };
		};
	}

	std::unique_ptr<RhiVulkan::VulkanShaderProgram> VulkanPipelineCapture::MakeProgram(Rhi::ShaderProgramInterfaceDesc interface)
	{
		// A SPIR-V header suffices for dispatch capture; real-driver tests compile Slang.
		const std::array<std::uint32_t, 5> header{ 0x07230203, 0x00010500, 0, 1, 0 };
		const auto bytes = std::as_bytes(std::span(header));
		const std::array<Rhi::ShaderStageArtifact, 2> stages{{
			{ Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes }
		}};
		return RhiVulkan::VulkanShaderProgram::Create(State, { stages, interface, {} });
	}

	std::unique_ptr<RhiVulkan::VulkanGraphicsPipeline> VulkanPipelineCapture::MakePipeline(Rhi::Format format)
	{
		auto program = MakeProgram();
		auto layout = RhiVulkan::VulkanPipelineLayout::Create(State, { program.get(), {} });
		Rhi::GraphicsPipelineDesc desc{};
		desc.Program = program.get();
		desc.Layout = layout.get();
		desc.ColorFormats = { &format, 1 };
		desc.DepthStencil.DepthTest = desc.DepthStencil.DepthWrite = false;
		return RhiVulkan::VulkanGraphicsPipeline::Create(State, desc);
	}

} // namespace Swim::Testing
