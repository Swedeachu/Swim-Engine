#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <type_traits>

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
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessTextureLayout,
			const VkVertexInputBindingDescription* bindingDescriptions,
			uint32_t bindingDescriptionCount,
			const VkVertexInputAttributeDescription* attributeDescriptions,
			uint32_t attributeDescriptionCount,
			uint32_t pushConstantSize
		);

		template<typename TBindings, typename TAttribs>
		void CreateGraphicsPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessTextureLayout,
			const TBindings& bindingDescriptions,
			const TAttribs& attributeDescriptions,
			uint32_t pushConstantSize
		)
		{
			CreateGraphicsPipeline(
				vertShaderPath,
				fragShaderPath,
				uboLayout,
				bindlessTextureLayout,
				bindingDescriptions.data(),
				static_cast<uint32_t>(bindingDescriptions.size()),
				attributeDescriptions.data(),
				static_cast<uint32_t>(attributeDescriptions.size()),
				pushConstantSize
			);
		}

		void CreateGpuDrivenGraphicsPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessTextureLayout,
			const VkVertexInputBindingDescription* bindingDescriptions,
			uint32_t bindingDescriptionCount,
			const VkVertexInputAttributeDescription* attributeDescriptions,
			uint32_t attributeDescriptionCount,
			uint32_t pushConstantSize
		);

		template<typename TBindings, typename TAttribs>
		void CreateGpuDrivenGraphicsPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessTextureLayout,
			const TBindings& bindingDescriptions,
			const TAttribs& attributeDescriptions,
			uint32_t pushConstantSize
		)
		{
			CreateGpuDrivenGraphicsPipeline(
				vertShaderPath,
				fragShaderPath,
				uboLayout,
				bindlessTextureLayout,
				bindingDescriptions.data(),
				static_cast<uint32_t>(bindingDescriptions.size()),
				attributeDescriptions.data(),
				static_cast<uint32_t>(attributeDescriptions.size()),
				pushConstantSize
			);
		}

		void CreateDecoratedMeshPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessLayout,
			const VkVertexInputBindingDescription* bindings,
			uint32_t bindingCount,
			const VkVertexInputAttributeDescription* attribs,
			uint32_t attribCount,
			uint32_t pushConstantSize
		);

		template<typename TBindings, typename TAttribs>
		void CreateDecoratedMeshPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessLayout,
			const TBindings& bindings,
			const TAttribs& attribs,
			uint32_t pushConstantSize
		)
		{
			CreateDecoratedMeshPipeline(
				vertShaderPath,
				fragShaderPath,
				uboLayout,
				bindlessLayout,
				bindings.data(),
				static_cast<uint32_t>(bindings.size()),
				attribs.data(),
				static_cast<uint32_t>(attribs.size()),
				pushConstantSize
			);
		}

		void CreateMsdfTextPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessLayout,
			const VkVertexInputBindingDescription* bindings,
			uint32_t bindingCount,
			const VkVertexInputAttributeDescription* attribs,
			uint32_t attribCount,
			uint32_t pushConstantSize
		);

		template<typename TBindings, typename TAttribs>
		void CreateMsdfTextPipeline(
			const std::string& vertShaderPath,
			const std::string& fragShaderPath,
			VkDescriptorSetLayout uboLayout,
			VkDescriptorSetLayout bindlessLayout,
			const TBindings& bindings,
			const TAttribs& attribs,
			uint32_t pushConstantSize
		)
		{
			CreateMsdfTextPipeline(
				vertShaderPath,
				fragShaderPath,
				uboLayout,
				bindlessLayout,
				bindings.data(),
				static_cast<uint32_t>(bindings.size()),
				attribs.data(),
				static_cast<uint32_t>(attribs.size()),
				pushConstantSize
			);
		}

		void CreateGpuCullComputePipeline(
			const std::string& computeShaderPath,
			VkDescriptorSetLayout descriptorSetLayout,
			uint32_t pushConstantSize
		);

		VkRenderPass GetRenderPass() const { return renderPass; }
		VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
		VkPipeline GetGraphicsPipeline() const { return graphicsPipeline; }
		VkPipeline GetGpuDrivenGraphicsPipeline() const { return gpuDrivenGraphicsPipeline; }
		bool HasGpuDrivenGraphicsPipeline() const { return gpuDrivenGraphicsPipeline != VK_NULL_HANDLE; }

		VkPipeline GetDecoratorPipeline() const { return decoratorPipeline; }
		VkPipelineLayout GetDecoratorPipelineLayout() const { return decoratorPipelineLayout; }

		VkPipeline GetMsdfTextPipeline() const { return msdfTextPipeline; }
		VkPipelineLayout GetMsdfTextPipelineLayout() const { return msdfTextPipelineLayout; }

		VkPipeline GetGpuCullComputePipeline() const { return gpuCullComputePipeline; }
		VkPipelineLayout GetGpuCullComputePipelineLayout() const { return gpuCullComputePipelineLayout; }
		bool HasGpuCullComputePipeline() const { return gpuCullComputePipeline != VK_NULL_HANDLE && gpuCullComputePipelineLayout != VK_NULL_HANDLE; }

		void Cleanup();

	private:

		VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		std::vector<char> ReadFile(const std::string& filename);

		VkDevice device;

		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		VkPipeline gpuDrivenGraphicsPipeline = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

		VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipeline decoratorPipeline = VK_NULL_HANDLE;
		VkPipelineLayout decoratorPipelineLayout = VK_NULL_HANDLE;

		VkPipeline msdfTextPipeline = VK_NULL_HANDLE;
		VkPipelineLayout msdfTextPipelineLayout = VK_NULL_HANDLE;

		VkPipeline gpuCullComputePipeline = VK_NULL_HANDLE;
		VkPipelineLayout gpuCullComputePipelineLayout = VK_NULL_HANDLE;

	};

}
