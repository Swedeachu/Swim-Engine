#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanGraphicsPipeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineUtils.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	VulkanGraphicsPipeline::VulkanGraphicsPipeline(std::shared_ptr<VulkanDeviceState> state, const Rhi::GraphicsPipelineDesc& desc)
		: state(std::move(state)), colorFormats(desc.ColorFormats.begin(), desc.ColorFormats.end()),
		  depthFormat(desc.DepthStencilFormat), samples(desc.Samples)
	{
	}

	VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
	{
		if (pipeline != VK_NULL_HANDLE)
		{
			state->Dispatch.vkDestroyPipeline(state->Device.device, pipeline, nullptr);
		}
	}

	std::unique_ptr<VulkanGraphicsPipeline> VulkanGraphicsPipeline::Create(
		std::shared_ptr<VulkanDeviceState> state, const Rhi::GraphicsPipelineDesc& desc)
	{
		auto* program = dynamic_cast<VulkanShaderProgram*>(desc.Program);
		auto* layout = dynamic_cast<VulkanPipelineLayout*>(desc.Layout);
		const auto& limits = state->Device.physical_device.properties.limits;
		if (program == nullptr || layout == nullptr || program->GetState() != state || layout->GetState() != state ||
			&layout->GetProgram() != program || program->GetStages().size() != 2 ||
			desc.ColorFormats.size() > limits.maxColorAttachments ||
			(desc.ColorFormats.empty() && desc.DepthStencilFormat == Rhi::Format::Undefined) ||
			(!desc.BlendAttachments.empty() && desc.BlendAttachments.size() != desc.ColorFormats.size()) ||
			desc.Raster.DepthClamp || desc.Raster.Wireframe ||
			(desc.Samples != Rhi::SampleCount::X1 && desc.Samples != Rhi::SampleCount::X2 &&
			 desc.Samples != Rhi::SampleCount::X4 && desc.Samples != Rhi::SampleCount::X8) ||
			(desc.DepthStencilFormat != Rhi::Format::Undefined && !Rhi::IsDepthFormat(desc.DepthStencilFormat)) ||
			(desc.DepthStencilFormat == Rhi::Format::Undefined && (desc.DepthStencil.DepthTest || desc.DepthStencil.DepthWrite)) ||
			(desc.DepthStencil.StencilTest && !Rhi::HasStencil(desc.DepthStencilFormat)))
		{
			return nullptr;
		}
		const auto nativeSamples = ToVkSampleCount(desc.Samples);
		if ((!desc.ColorFormats.empty() && (limits.framebufferColorSampleCounts & nativeSamples) == 0) ||
			(Rhi::IsDepthFormat(desc.DepthStencilFormat) && (limits.framebufferDepthSampleCounts & nativeSamples) == 0) ||
			(Rhi::HasStencil(desc.DepthStencilFormat) && (limits.framebufferStencilSampleCounts & nativeSamples) == 0))
		{
			return nullptr;
		}

		if (desc.DepthStencilFormat != Rhi::Format::Undefined)
		{
			VkFormatProperties properties{};
			state->Instance->Dispatch.vkGetPhysicalDeviceFormatProperties(state->Device.physical_device.physical_device,
				ToVkFormat(desc.DepthStencilFormat), &properties);
			if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0)
			{
				return nullptr;
			}
		}

		std::vector<VkFormat> formats;
		std::vector<VkPipelineColorBlendAttachmentState> blends;
		try
		{
			for (std::size_t index = 0; index < desc.ColorFormats.size(); ++index)
			{
				const auto format = desc.ColorFormats[index];
				if (ToVkFormat(format) == VK_FORMAT_UNDEFINED || Rhi::IsDepthFormat(format))
				{
					return nullptr;
				}
				formats.push_back(ToVkFormat(format));
				const auto blend = desc.BlendAttachments.empty() ? Rhi::BlendAttachmentState{} : desc.BlendAttachments[index];
				if ((static_cast<std::uint32_t>(blend.WriteMask) & ~15u) != 0)
				{
					return nullptr;
				}
				VkPipelineColorBlendAttachmentState native{};
				native.blendEnable = blend.Enabled;
				native.srcColorBlendFactor = ToVkBlendFactor(blend.SourceColor);
				native.dstColorBlendFactor = ToVkBlendFactor(blend.DestinationColor);
				native.colorBlendOp = ToVkBlendOp(blend.ColorOperation);
				native.srcAlphaBlendFactor = ToVkBlendFactor(blend.SourceAlpha);
				native.dstAlphaBlendFactor = ToVkBlendFactor(blend.DestinationAlpha);
				native.alphaBlendOp = ToVkBlendOp(blend.AlphaOperation);
				native.colorWriteMask = static_cast<VkColorComponentFlags>(blend.WriteMask);
				// independentBlend is not enabled in the device baseline.
				if (!blends.empty())
				{
					const auto& first = blends.front();
					if (native.blendEnable != first.blendEnable || native.srcColorBlendFactor != first.srcColorBlendFactor ||
						native.dstColorBlendFactor != first.dstColorBlendFactor || native.colorBlendOp != first.colorBlendOp ||
						native.srcAlphaBlendFactor != first.srcAlphaBlendFactor || native.dstAlphaBlendFactor != first.dstAlphaBlendFactor ||
						native.alphaBlendOp != first.alphaBlendOp || native.colorWriteMask != first.colorWriteMask)
					{
						return nullptr;
					}
				}
				VkFormatProperties properties{};
				state->Instance->Dispatch.vkGetPhysicalDeviceFormatProperties(state->Device.physical_device.physical_device, formats.back(), &properties);
				const auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | (blend.Enabled ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT : 0);
				if ((properties.optimalTilingFeatures & required) != static_cast<VkFormatFeatureFlags>(required))
				{
					return nullptr;
				}
				blends.push_back(native);
			}

			std::vector<VkPipelineShaderStageCreateInfo> stages;
			for (const auto& stage : program->GetStages())
			{
				VkPipelineShaderStageCreateInfo native{};
				native.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				native.stage = stage.Stage;
				native.module = stage.Module;
				native.pName = stage.EntryPoint.c_str();
				stages.push_back(native);
			}
			VkPipelineVertexInputStateCreateInfo vertex{};
			vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			// This first draw path uses SV_VertexID; explicit vertex layouts follow with resource bindings.
			VkPipelineInputAssemblyStateCreateInfo assembly{};
			assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			assembly.topology = ToVkPrimitiveTopology(desc.Topology);
			VkPipelineViewportStateCreateInfo viewport{};
			viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewport.viewportCount = viewport.scissorCount = 1;
			VkPipelineRasterizationStateCreateInfo raster{};
			raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.cullMode = ToVkCullMode(desc.Raster.Cull);
			raster.frontFace = ToVkFrontFace(desc.Raster.Winding);
			raster.lineWidth = 1.0f;
			VkPipelineMultisampleStateCreateInfo multisample{};
			multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisample.rasterizationSamples = nativeSamples;
			VkPipelineDepthStencilStateCreateInfo depth{};
			depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depth.depthTestEnable = desc.DepthStencil.DepthTest;
			depth.depthWriteEnable = desc.DepthStencil.DepthWrite;
			depth.depthCompareOp = ToVkCompareOp(desc.DepthStencil.DepthCompare);
			depth.stencilTestEnable = desc.DepthStencil.StencilTest;
			const auto stencil = [](const Rhi::StencilFaceState& face)
			{
				return VkStencilOpState{ ToVkStencilOp(face.Fail), ToVkStencilOp(face.Pass), ToVkStencilOp(face.DepthFail),
					ToVkCompareOp(face.Compare), face.CompareMask, face.WriteMask, face.Reference };
			};
			depth.front = stencil(desc.DepthStencil.Front);
			depth.back = stencil(desc.DepthStencil.Back);
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = static_cast<std::uint32_t>(blends.size());
			blend.pAttachments = blends.data();
			const std::array dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamic{};
			dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
			dynamic.pDynamicStates = dynamicStates.data();
			VkPipelineRenderingCreateInfo rendering{};
			rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
			rendering.colorAttachmentCount = static_cast<std::uint32_t>(formats.size());
			rendering.pColorAttachmentFormats = formats.data();
			rendering.depthAttachmentFormat = ToVkFormat(desc.DepthStencilFormat);
			rendering.stencilAttachmentFormat = Rhi::HasStencil(desc.DepthStencilFormat) ? rendering.depthAttachmentFormat : VK_FORMAT_UNDEFINED;
			VkGraphicsPipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			info.pNext = &rendering;
			info.stageCount = static_cast<std::uint32_t>(stages.size());
			info.pStages = stages.data();
			info.pVertexInputState = &vertex;
			info.pInputAssemblyState = &assembly;
			info.pViewportState = &viewport;
			info.pRasterizationState = &raster;
			info.pMultisampleState = &multisample;
			info.pDepthStencilState = &depth;
			info.pColorBlendState = &blend;
			info.pDynamicState = &dynamic;
			info.layout = FromNativeHandle<VkPipelineLayout>(layout->GetNativeHandle());
			auto result = std::make_unique<VulkanGraphicsPipeline>(state, desc);
			result->layoutState = layout->GetLayoutState();
			if (state->Dispatch.vkCreateGraphicsPipelines(state->Device.device, VK_NULL_HANDLE, 1, &info, nullptr, &result->pipeline) != VK_SUCCESS)
			{
				// Vulkan may return a partial pipeline on failure; RAII destroys it.
				return nullptr;
			}
			SetVulkanObjectName(*state, VK_OBJECT_TYPE_PIPELINE, ToNativeHandle(result->pipeline), desc.DebugName);
			return result;
		}
		catch (const std::invalid_argument&)
		{
			return nullptr;
		}
	}

	std::uintptr_t VulkanGraphicsPipeline::GetNativeHandle() const
	{
		return ToNativeHandle(pipeline);
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanGraphicsPipeline::GetState() const
	{
		return state;
	}

	const std::shared_ptr<VulkanPipelineLayoutState>& VulkanGraphicsPipeline::GetLayoutState() const
	{
		return layoutState;
	}

	bool VulkanGraphicsPipeline::MatchesRendering(std::span<const Rhi::Format> colors, Rhi::Format depth, Rhi::SampleCount sampleCount) const
	{
		return std::equal(colorFormats.begin(), colorFormats.end(), colors.begin(), colors.end()) && depthFormat == depth && samples == sampleCount;
	}

} // namespace Swim::RhiVulkan
