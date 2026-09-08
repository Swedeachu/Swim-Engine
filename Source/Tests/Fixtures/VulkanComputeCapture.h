#pragma once

#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanComputePipeline.h"

namespace Swim::Testing
{

	struct VulkanComputeCapture : VulkanDescriptorCapture
	{
		VulkanComputeCapture();
		std::unique_ptr<RhiVulkan::VulkanShaderProgram> MakeComputeProgram(Rhi::ShaderProgramInterfaceDesc interface = {},
			std::array<std::uint32_t, 3> local = { 8, 4, 1 });
		std::unique_ptr<RhiVulkan::VulkanComputePipeline> MakeComputePipeline(Rhi::ShaderProgramInterfaceDesc interface = {});
		VkShaderStageFlagBits ComputeStage{};
		VkShaderModule ComputeModule = VK_NULL_HANDLE;
		VkPipelineLayout ComputeLayout = VK_NULL_HANDLE;
		std::string ComputeEntry;
		VkPipelineBindPoint BindPoint{};
		std::vector<std::array<std::uint32_t, 3>> Dispatches;
	};

} // namespace Swim::Testing
