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

		void CreateDecoratedMeshPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessLayout,
			const std::vector<VkVertexInputBindingDescription>& bindings,
			const std::vector<VkVertexInputAttributeDescription>& attribs,
			uint32_t pushConstantSize
		);

		void CreateMsdfTextPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessLayout,
			const std::vector<VkVertexInputBindingDescription>& bindings,
			const std::vector<VkVertexInputAttributeDescription>& attribs,
			uint32_t pushConstantSize // optional, can be 0
		);

		VkRenderPass GetRenderPass() const { return renderPass; }
		VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
		VkPipeline GetGraphicsPipeline() const { return graphicsPipeline; }

		VkPipeline GetDecoratorPipeline() const { return decoratorPipeline; }
		VkPipelineLayout GetDecoratorPipelineLayout() const { return decoratorPipelineLayout; }

		VkPipeline GetMsdfTextPipeline() const { return msdfTextPipeline; }
		VkPipelineLayout GetMsdfTextPipelineLayout() const { return msdfTextPipelineLayout; }

		void Cleanup();

	private:

		VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		std::vector<char> ReadFile(const std::string& filename);

		VkDevice device;

		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

		VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipeline decoratorPipeline = VK_NULL_HANDLE;
		VkPipelineLayout decoratorPipelineLayout = VK_NULL_HANDLE;

		VkPipeline msdfTextPipeline = VK_NULL_HANDLE;
		VkPipelineLayout msdfTextPipelineLayout = VK_NULL_HANDLE;

	};

}
