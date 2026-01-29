#pragma once

#include "PCH.h"

namespace Engine
{

	class VulkanPipelineManager
	{

	public:

		VulkanPipelineManager(VkDevice device);

		void CreateRenderPass(VkFormat swapChainImageFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);

		void CreateGraphicsPipeline(
			const std::string& vertPath,
			const std::string& fragPath,
			VkDescriptorSetLayout set0Layout,
			VkDescriptorSetLayout set1BindlessLayout,
			const std::vector<VkVertexInputBindingDescription>& bindings,
			const std::vector<VkVertexInputAttributeDescription>& attribs,
			uint32_t instanceStride
		);

		void CreateDecoratedMeshPipeline(
			const std::string& vertPath,
			const std::string& fragPath,
			VkDescriptorSetLayout set0Layout,
			VkDescriptorSetLayout set1BindlessLayout,
			const std::vector<VkVertexInputBindingDescription>& bindings,
			const std::vector<VkVertexInputAttributeDescription>& attribs,
			uint32_t instanceStride
		);

		void CreateMsdfTextPipeline(
			const std::string& vertPath,
			const std::string& fragPath,
			VkDescriptorSetLayout set0Layout,
			VkDescriptorSetLayout set1BindlessLayout,
			const std::vector<VkVertexInputBindingDescription>& bindings,
			const std::vector<VkVertexInputAttributeDescription>& attribs,
			uint32_t instanceStride
		);

		// Compute culling pipeline (writes indirect commands + draw count)
		void CreateCullComputePipeline(const std::string& compPath, VkDescriptorSetLayout set0Layout);

		VkRenderPass GetRenderPass() const { return renderPass; }

		VkPipeline GetGraphicsPipeline() const { return graphicsPipeline; }
		VkPipelineLayout GetPipelineLayout() const { return graphicsPipelineLayout; }

		VkPipeline GetDecoratorPipeline() const { return decoratorPipeline; }
		VkPipelineLayout GetDecoratorPipelineLayout() const { return decoratorPipelineLayout; }

		VkPipeline GetMsdfTextPipeline() const { return msdfTextPipeline; }
		VkPipelineLayout GetMsdfTextPipelineLayout() const { return msdfTextPipelineLayout; }

		VkPipeline GetCullComputePipeline() const { return cullComputePipeline; }
		VkPipelineLayout GetCullComputePipelineLayout() const { return cullComputePipelineLayout; }

		void Cleanup();

	private:

		VkShaderModule CreateShaderModule(const std::vector<char>& code);

		static std::vector<char> ReadFile(const std::string& filename);

		VkDevice device;

		VkRenderPass renderPass = VK_NULL_HANDLE;

		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;

		VkPipeline decoratorPipeline = VK_NULL_HANDLE;
		VkPipelineLayout decoratorPipelineLayout = VK_NULL_HANDLE;

		VkPipeline msdfTextPipeline = VK_NULL_HANDLE;
		VkPipelineLayout msdfTextPipelineLayout = VK_NULL_HANDLE;

		VkPipeline cullComputePipeline = VK_NULL_HANDLE;
		VkPipelineLayout cullComputePipelineLayout = VK_NULL_HANDLE;

		VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	};

}
