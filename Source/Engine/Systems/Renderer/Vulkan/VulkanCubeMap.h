#pragma once

#include "Engine/Systems/Renderer/Core/Environment/CubeMap.h"
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"
#include "vulkan/vulkan.h"

namespace Engine
{

	class VulkanCubeMap : public CubeMap
	{

	public:

		VulkanCubeMap(const std::string& vertShaderPath, const std::string& fragShaderPath);
		~VulkanCubeMap();

		void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;

		// We use this render method instead of the overriden one
		void Render(VkCommandBuffer cmd, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

		void SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces) override;

	private:

		VkDevice device = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

		VkImage cubemapImage = VK_NULL_HANDLE;
		VkDeviceMemory cubemapMemory = VK_NULL_HANDLE;
		VkImageView cubemapImageView = VK_NULL_HANDLE;
		VkSampler cubemapSampler = VK_NULL_HANDLE;

		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;

		std::string vertShaderPath;
		std::string fragShaderPath;

		VkSampleCountFlagBits samples;

		void CreateVertexBuffer();
		void CreateCubemapImageFromTextures(const std::array<std::shared_ptr<Texture2D>, 6>& textures);
		void CreateDescriptorSetLayout();
		void CreateDescriptorPool();
		void AllocateAndWriteDescriptorSet();
		void CreatePipelineForSkybox();
		void DestroyCubemapResources();

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

		std::vector<char> ReadFile(const std::string& filename);

		VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

	};

}
