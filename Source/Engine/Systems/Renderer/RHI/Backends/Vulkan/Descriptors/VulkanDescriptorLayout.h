#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	struct VulkanPipelineLayoutState
	{
		~VulkanPipelineLayoutState();
		std::shared_ptr<VulkanDeviceState> Device;
		Rhi::ShaderProgramInterface Interface;
		std::vector<VkDescriptorSetLayout> Sets;
		VkPipelineLayout Layout = VK_NULL_HANDLE;
	};

	VkDescriptorType ToVkDescriptorType(Rhi::DescriptorType type);
	VkShaderStageFlags ToVkDescriptorStages(Rhi::ShaderStageMask stages);
	const Rhi::DescriptorSchemaDesc* FindDescriptorSchema(const VulkanPipelineLayoutState& layout, std::uint32_t space);
	bool CreateDescriptorLayouts(VulkanPipelineLayoutState& layout);

} // namespace Swim::RhiVulkan
