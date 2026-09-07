#pragma once

#include "Tests/Fixtures/VulkanCommandCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanGraphicsPipeline.h"

#include <array>

namespace Swim::Testing
{

	struct VulkanPipelineCapture : VulkanCommandCapture
	{
		VulkanPipelineCapture();
		~VulkanPipelineCapture();
		std::unique_ptr<RhiVulkan::VulkanShaderProgram> MakeProgram(Rhi::ShaderProgramInterfaceDesc interface = {});
		std::unique_ptr<RhiVulkan::VulkanGraphicsPipeline> MakePipeline(Rhi::Format format = Rhi::Format::RGBA8Unorm,
			std::span<const Rhi::VertexBindingDesc> bindings = {}, std::span<const Rhi::VertexAttributeDesc> attributes = {});

		std::uint32_t CachesCreated = 0;
		std::uint32_t CachesDestroyed = 0;
		VkPipelineCache LastPipelineCache = VK_NULL_HANDLE;
		std::uint32_t ModulesCreated = 0;
		std::uint32_t ModulesDestroyed = 0;
		std::uint32_t FailModule = 0;
		std::uint32_t LayoutsCreated = 0;
		std::uint32_t LayoutsDestroyed = 0;
		std::uint32_t PipelinesCreated = 0;
		std::uint32_t PipelinesDestroyed = 0;
		std::uint32_t BindCount = 0;
		std::uint32_t DrawCount = 0;
		std::uint32_t IndexedDrawCount = 0;
		VkResult PipelineResult = VK_SUCCESS;
		VkFormatFeatureFlags FormatFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
		std::vector<VkFormat> PipelineColors;
		VkFormat PipelineDepth = VK_FORMAT_UNDEFINED;
		VkFormat PipelineStencil = VK_FORMAT_UNDEFINED;
		VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkFrontFace Winding = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		VkPipelineColorBlendAttachmentState Blend{};
		VkPipelineDepthStencilStateCreateInfo DepthState{};
		std::vector<VkDynamicState> DynamicStates;
		VkFormatFeatureFlags VertexFormatFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
		std::vector<VkVertexInputBindingDescription> VertexBindings;
		std::vector<VkVertexInputAttributeDescription> VertexAttributes;
		std::uint32_t VertexBindCount = 0;
		std::uint32_t LastVertexSlot = 0;
		VkBuffer LastVertexBuffer = VK_NULL_HANDLE;
		VkDeviceSize LastVertexOffset = 0;
		VkDrawIndirectCommand Draw{};
		VkDrawIndexedIndirectCommand IndexedDraw{};
	};

} // namespace Swim::Testing
