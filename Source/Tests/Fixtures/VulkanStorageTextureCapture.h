#pragma once

#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

namespace Swim::Testing
{

	struct VulkanStorageTextureCapture : VulkanComputeCapture
	{
		VulkanStorageTextureCapture();
		Rhi::DescriptorSchemaDesc Schema{ 1, { { 7, Rhi::DescriptorType::StorageTexture, 1,
			Rhi::ShaderStageMask::Compute, false, false, Rhi::Format::RGBA32Float } } };
		std::unique_ptr<RhiVulkan::VulkanShaderProgram> Program;
		std::unique_ptr<RhiVulkan::VulkanPipelineLayout> Layout;
		std::unique_ptr<RhiVulkan::VulkanDescriptorTable> Table;
		VkImageFormatProperties ImageProperties{};
		VkResult ImageResult = VK_SUCCESS;
		std::uint32_t ImageQueries = 0;
		VkImageUsageFlags ImageUsage = 0;
		VkFormat ImageFormat = VK_FORMAT_UNDEFINED;
		void CreateTable();
	};

} // namespace Swim::Testing
