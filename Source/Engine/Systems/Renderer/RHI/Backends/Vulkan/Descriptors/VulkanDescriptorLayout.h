#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <span>

namespace Swim::RhiVulkan
{

	struct VulkanPipelineLayoutState
	{
		~VulkanPipelineLayoutState();
		std::shared_ptr<VulkanDeviceState> Device;
		Rhi::ShaderProgramInterface Interface;
		Rhi::ShaderStageMask ProgramStages = Rhi::ShaderStageMask::None;
		std::vector<VkDescriptorSetLayout> Sets;
		// Per space: created with UPDATE_AFTER_BIND_POOL (its tables need a matching pool).
		std::vector<bool> UpdateAfterBindSets;
		std::vector<VkPushConstantRange> PushConstants;
		VkPipelineLayout Layout = VK_NULL_HANDLE;
	};

	VkDescriptorType ToVkDescriptorType(Rhi::DescriptorType type);
	VkShaderStageFlags ToVkDescriptorStages(Rhi::ShaderStageMask stages);
	const Rhi::DescriptorSchemaDesc* FindDescriptorSchema(const VulkanPipelineLayoutState& layout, std::uint32_t space);
	// Replaces reflected spaces by compatible explicit ones (PipelineLayoutDesc::DescriptorSpaces).
	bool CreateDescriptorLayouts(VulkanPipelineLayoutState& layout, std::span<const Rhi::DescriptorSchemaDesc> explicitSpaces = {});
	bool SameDescriptorBinding(const Rhi::DescriptorBindingDesc& a, const Rhi::DescriptorBindingDesc& b);
	// Vulkan set compatibility: identically defined layouts for this space.
	bool AreDescriptorSpacesCompatible(const VulkanPipelineLayoutState& a, const VulkanPipelineLayoutState& b, std::uint32_t space);

} // namespace Swim::RhiVulkan
