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

		void CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits sampleCount);

		void CreateGraphicsPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout, // Set 0
			VkDescriptorSetLayout bindlessTextureLayout, // Set 1
			const std::vector<VkVertexInputBindingDescription>& bindingDescriptions, 
			const std::vector<VkVertexInputAttributeDescription>& attributeDescriptions,
			uint32_t pushConstantSize
		);

		VkRenderPass GetRenderPass() const { return renderPass; }
		VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
		VkPipeline GetGraphicsPipeline() const { return graphicsPipeline; }

		void Cleanup();

	private:

		VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		std::vector<char> ReadFile(const std::string& filename);

		VkDevice device;

		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

		VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	};

}
