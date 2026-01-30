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

		// Compute culling pipeline (legacy: writes 1 command per range)
		void CreateCullComputePipeline(const std::string& compPath, VkDescriptorSetLayout set0Layout);

		// True-batched compute culling pipelines (6 entry points)
		void CreateCullTrueBatchComputePipelines(
			const std::string& countPath,
			const char* countEntry,
			const std::string& scan512Path,
			const char* scan512Entry,
			const std::string& scanGroupsPath,
			const char* scanGroupsEntry,
			const std::string& fixupPath,
			const char* fixupEntry,
			const std::string& scatterPath,
			const char* scatterEntry,
			const std::string& buildPath,
			const char* buildEntry,
			VkDescriptorSetLayout set0Layout
		);

		VkRenderPass GetRenderPass() const { return renderPass; }

		VkPipeline GetGraphicsPipeline() const { return graphicsPipeline; }
		VkPipelineLayout GetPipelineLayout() const { return graphicsPipelineLayout; }

		VkPipeline GetDecoratorPipeline() const { return decoratorPipeline; }
		VkPipelineLayout GetDecoratorPipelineLayout() const { return decoratorPipelineLayout; }

		VkPipeline GetMsdfTextPipeline() const { return msdfTextPipeline; }
		VkPipelineLayout GetMsdfTextPipelineLayout() const { return msdfTextPipelineLayout; }

		VkPipeline GetCullComputePipeline() const { return cullComputePipeline; }
		VkPipelineLayout GetCullComputePipelineLayout() const { return cullComputePipelineLayout; }

		// True-batched compute getters
		VkPipelineLayout GetCullTrueBatchPipelineLayout() const { return cullTrueBatchPipelineLayout; }

		VkPipeline GetCullTrueBatchCountPipeline() const { return cullTrueBatchCountPipeline; }
		VkPipeline GetCullTrueBatchScan512Pipeline() const { return cullTrueBatchScan512Pipeline; }
		VkPipeline GetCullTrueBatchScanGroupsPipeline() const { return cullTrueBatchScanGroupsPipeline; }
		VkPipeline GetCullTrueBatchFixupPipeline() const { return cullTrueBatchFixupPipeline; }
		VkPipeline GetCullTrueBatchScatterPipeline() const { return cullTrueBatchScatterPipeline; }
		VkPipeline GetCullTrueBatchBuildPipeline() const { return cullTrueBatchBuildPipeline; }

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

		// True-batched compute (6 pipelines, 1 layout)
		VkPipelineLayout cullTrueBatchPipelineLayout = VK_NULL_HANDLE;

		VkPipeline cullTrueBatchCountPipeline = VK_NULL_HANDLE;
		VkPipeline cullTrueBatchScan512Pipeline = VK_NULL_HANDLE;
		VkPipeline cullTrueBatchScanGroupsPipeline = VK_NULL_HANDLE;
		VkPipeline cullTrueBatchFixupPipeline = VK_NULL_HANDLE;
		VkPipeline cullTrueBatchScatterPipeline = VK_NULL_HANDLE;
		VkPipeline cullTrueBatchBuildPipeline = VK_NULL_HANDLE;

		VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	};

}
