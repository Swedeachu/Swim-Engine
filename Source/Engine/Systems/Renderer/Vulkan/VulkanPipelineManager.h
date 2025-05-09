#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Engine
{

	class VulkanPipelineManager
	{

	public:

		VulkanPipelineManager(VkDevice device);
		~VulkanPipelineManager();

		void CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat);

		void CreateGraphicsPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout, // Set 0
			VkDescriptorSetLayout bindlessTextureLayout, // Set 1
			const std::vector<VkVertexInputBindingDescription>& bindingDescriptions, 
			const std::vector<VkVertexInputAttributeDescription>& attributeDescriptions,
			uint32_t pushConstantSize
		);

		void CreateComputePipeline(const std::string& computeShaderPath, VkDescriptorSetLayout descriptorLayout);

		VkRenderPass GetRenderPass() const { return renderPass; }
		VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
		VkPipeline GetGraphicsPipeline() const { return graphicsPipeline; }

		VkPipeline GetComputePipeline() const { return computePipeline; }
		VkPipelineLayout GetComputePipelineLayout() const { return computePipelineLayout; }

		void Cleanup();

	private:

		VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		std::vector<char> ReadFile(const std::string& filename);

		VkDevice device;

		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

		VkPipeline computePipeline = VK_NULL_HANDLE;
		VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;

	};

}
