#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

namespace Swim::RhiVulkan
{

	struct VulkanResourceState
	{
		VkPipelineStageFlags2 Stages = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 Access = VK_ACCESS_2_NONE;
		VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	VulkanResourceState GetBufferState(const Rhi::BufferDesc& desc, Rhi::ResourceState state);
	VulkanResourceState GetTextureState(const Rhi::TextureDesc& desc, Rhi::ResourceState state);

} // namespace Swim::RhiVulkan
