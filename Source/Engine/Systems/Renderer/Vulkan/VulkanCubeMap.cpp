#include "PCH.h"
#include "VulkanCubeMap.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "fstream"
#include "Library/stb/stb_image_resize2_wrapper.h"

namespace Engine
{

	static constexpr VkDeviceSize kVertexBufferSize = sizeof(skyboxVerticesInward);

	VulkanCubeMap::VulkanCubeMap(const std::string& vertPath, const std::string& fragPath)
		: CubeMap(vertPath, fragPath), vertShaderPath(vertPath), fragShaderPath(fragPath)
	{
		auto& renderer = SwimEngine::GetInstance()->GetVulkanRenderer();
		device = renderer->GetDevice();
		physicalDevice = renderer->GetPhysicalDevice();

		CreateVertexBuffer();
		CreateDescriptorSetLayout();
		CreateDescriptorPool();
		CreatePipelineForSkybox();
	}

	VulkanCubeMap::~VulkanCubeMap()
	{
		vkDestroySampler(device, cubemapSampler, nullptr);
		vkDestroyImageView(device, cubemapImageView, nullptr);
		vkDestroyImage(device, cubemapImage, nullptr);
		vkFreeMemory(device, cubemapMemory, nullptr);

		vkDestroyBuffer(device, vertexBuffer, nullptr);
		vkFreeMemory(device, vertexMemory, nullptr);

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	}

	uint32_t VulkanCubeMap::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		{
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type!");
	}

	void VulkanCubeMap::CreateVertexBuffer()
	{
		VkDeviceSize bufferSize = sizeof(skyboxVerticesInward);

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = bufferSize;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
		vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

		void* data;
		vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
		memcpy(data, skyboxVerticesInward, (size_t)bufferSize);
		vkUnmapMemory(device, stagingMemory);

		// GPU Vertex Buffer
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer);

		vkGetBufferMemoryRequirements(device, vertexBuffer, &memReq);
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		vkAllocateMemory(device, &allocInfo, nullptr, &vertexMemory);
		vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);

		// Copy via one-time command
		auto renderer = SwimEngine::GetInstance()->GetVulkanRenderer();
		VkCommandBuffer cmd = renderer->GetCommandManager()->BeginSingleTimeCommands();

		VkBufferCopy copyRegion{};
		copyRegion.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer, vertexBuffer, 1, &copyRegion);

		renderer->GetCommandManager()->EndSingleTimeCommands(cmd, renderer->GetDeviceManager()->GetGraphicsQueue());

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingMemory, nullptr);
	}

	void VulkanCubeMap::CreateCubemapImageFromTextures(const std::array<std::shared_ptr<Texture2D>, 6>& textures)
	{
		// === Step 1: Validate input textures ===
		uint32_t maxDim = 0;

		for (int i = 0; i < 6; ++i)
		{
			if (!textures[i])
			{
				throw std::runtime_error("Texture face " + std::to_string(i) + " is null.");
			}

			if (textures[i]->GetData() == nullptr)
			{
				throw std::runtime_error("Texture face " + std::to_string(i) + " has null pixel data.");
			}

			uint32_t w = textures[i]->GetWidth();
			uint32_t h = textures[i]->GetHeight();

			if (w == 0 || h == 0)
			{
				throw std::runtime_error("Texture face " + std::to_string(i) + " has zero dimensions.");
			}

			maxDim = std::max({ maxDim, w, h });
		}

		// === Step 2: Resize to common POT dimensions ===
		auto LargestPowerOfTwoBelowOrEqual = [](uint32_t x) -> uint32_t
		{
			uint32_t pot = 1;
			while ((pot << 1) <= x) { pot <<= 1; }
			return pot;
		};

		const uint32_t faceSize = LargestPowerOfTwoBelowOrEqual(maxDim);
		const VkDeviceSize imageSize = faceSize * faceSize * 4;
		const VkDeviceSize totalSize = imageSize * 6;
		const VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

		std::vector<unsigned char> resizedFacesData(totalSize);

		for (int face = 0; face < 6; ++face)
		{
			const auto& tex = textures[face];

			const int srcW = static_cast<int>(tex->GetWidth());
			const int srcH = static_cast<int>(tex->GetHeight());
			const int srcStride = srcW * 4;
			const int dstStride = faceSize * 4;

			unsigned char* src = tex->GetData();
			unsigned char* dst = resizedFacesData.data() + face * imageSize;

			bool success = stbir_resize_uint8_linear(
				src, srcW, srcH, srcStride,
				dst, static_cast<int>(faceSize), static_cast<int>(faceSize), dstStride,
				STBIR_RGBA
			);

			if (!success)
			{
				throw std::runtime_error("Failed to resize cubemap face " + std::to_string(face));
			}

			// Optionally rotate top/bottom if needed
			if (face == 2 || face == 3)
			{
				RotateImage180(dst, faceSize, faceSize);
			}
		}

		// === Step 3: Destroy old resources if present ===
		if (cubemapImage != VK_NULL_HANDLE)
		{
			vkDestroyImage(device, cubemapImage, nullptr);
			vkFreeMemory(device, cubemapMemory, nullptr);
			vkDestroyImageView(device, cubemapImageView, nullptr);
			vkDestroySampler(device, cubemapSampler, nullptr);
		}

		// === Step 4: Create staging buffer ===
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = totalSize;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
		vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

		void* mappedData = nullptr;
		vkMapMemory(device, stagingMemory, 0, totalSize, 0, &mappedData);
		std::memcpy(mappedData, resizedFacesData.data(), totalSize);
		vkUnmapMemory(device, stagingMemory);

		// === Step 5: Create cubemap image ===
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { faceSize, faceSize, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 6;
		imageInfo.format = format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		vkCreateImage(device, &imageInfo, nullptr, &cubemapImage);

		vkGetImageMemoryRequirements(device, cubemapImage, &memReq);
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		vkAllocateMemory(device, &allocInfo, nullptr, &cubemapMemory);
		vkBindImageMemory(device, cubemapImage, cubemapMemory, 0);

		// === Step 6: Copy data to cubemap ===
		auto cmdMgr = SwimEngine::GetInstance()->GetVulkanRenderer()->GetCommandManager().get();
		VkQueue graphicsQueue = SwimEngine::GetInstance()->GetVulkanRenderer()->GetDeviceManager()->GetGraphicsQueue();
		VkCommandBuffer cmd = cmdMgr->BeginSingleTimeCommands();

		// Transition image layout
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = cubemapImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 6;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		// Prepare buffer-to-image copy regions
		std::array<VkBufferImageCopy, 6> copyRegions{};
		for (uint32_t i = 0; i < 6; ++i)
		{
			copyRegions[i].bufferOffset = imageSize * i;
			copyRegions[i].bufferRowLength = 0;
			copyRegions[i].bufferImageHeight = 0;
			copyRegions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1 };
			copyRegions[i].imageOffset = { 0, 0, 0 };
			copyRegions[i].imageExtent = { faceSize, faceSize, 1 };
		}

		vkCmdCopyBufferToImage(cmd, stagingBuffer, cubemapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, copyRegions.data());

		// Transition to shader-readable layout
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		cmdMgr->EndSingleTimeCommands(cmd, graphicsQueue);

		// === Step 7: Create image view and sampler ===
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = cubemapImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewInfo.format = format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };

		vkCreateImageView(device, &viewInfo, nullptr, &cubemapImageView);

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		vkCreateSampler(device, &samplerInfo, nullptr, &cubemapSampler);

		// === Step 8: Cleanup staging buffer ===
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingMemory, nullptr);
	}

	void VulkanCubeMap::SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& newFaces)
	{
		// Ensure GPU is idle before replacing image
		vkDeviceWaitIdle(device);

		// Destroy previous cubemap resources safely
		DestroyCubemapResources();

		// Reassign the face handles
		faces = newFaces;

		std::array<std::shared_ptr<Texture2D>, 6> ordered;
		for (int i = 0; i < 6; ++i)
		{
			ordered[i] = faces[faceOrder[i]];
		}

		// Create new cubemap resources
		CreateCubemapImageFromTextures(ordered);

		// Allocate and write the descriptor set
		AllocateAndWriteDescriptorSet();
	}

	void VulkanCubeMap::DestroyCubemapResources()
	{
		if (descriptorSet != VK_NULL_HANDLE)
		{
			// We don't explicitly free individual descriptor sets in Vulkan,
			// but we can reset the handle so we don't reuse it.
			descriptorSet = VK_NULL_HANDLE;
		}

		if (cubemapSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, cubemapSampler, nullptr);
			cubemapSampler = VK_NULL_HANDLE;
		}

		if (cubemapImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, cubemapImageView, nullptr);
			cubemapImageView = VK_NULL_HANDLE;
		}

		if (cubemapImage != VK_NULL_HANDLE)
		{
			vkDestroyImage(device, cubemapImage, nullptr);
			cubemapImage = VK_NULL_HANDLE;
		}

		if (cubemapMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, cubemapMemory, nullptr);
			cubemapMemory = VK_NULL_HANDLE;
		}
	}

	void VulkanCubeMap::CreateDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorCount = 1;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding imageBinding{};
		imageBinding.binding = 1;
		imageBinding.descriptorCount = 1;
		imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { samplerBinding, imageBinding };

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
	}

	void VulkanCubeMap::CreateDescriptorPool()
	{
		constexpr uint32_t maxSets = 8; // leg room

		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0] = { VK_DESCRIPTOR_TYPE_SAMPLER, maxSets };
		poolSizes[1] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets };

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = maxSets;

		vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
	}

	void VulkanCubeMap::AllocateAndWriteDescriptorSet()
	{
		// === Destroy and recreate descriptor pool to ensure fresh capacity ===
		if (descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
		}
		CreateDescriptorPool(); // Recreate with fresh capacity

		// === Allocate new descriptor set ===
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
		if (result != VK_SUCCESS)
		{
			throw std::runtime_error("VulkanCubeMap::AllocateAndWriteDescriptorSet: Failed to allocate descriptor set! VkResult = " + std::to_string(result));
		}

		// === Ensure required handles are valid ===
		if (cubemapSampler == VK_NULL_HANDLE || cubemapImageView == VK_NULL_HANDLE)
		{
			throw std::runtime_error("VulkanCubeMap::AllocateAndWriteDescriptorSet: Sampler or ImageView is null!");
		}

		VkDescriptorImageInfo samplerInfo{};
		samplerInfo.sampler = cubemapSampler;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = cubemapImageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet samplerWrite{};
		samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		samplerWrite.dstSet = descriptorSet;
		samplerWrite.dstBinding = 0;
		samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerWrite.descriptorCount = 1;
		samplerWrite.pImageInfo = &samplerInfo;

		VkWriteDescriptorSet imageWrite{};
		imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		imageWrite.dstSet = descriptorSet;
		imageWrite.dstBinding = 1;
		imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		imageWrite.descriptorCount = 1;
		imageWrite.pImageInfo = &imageInfo;

		std::array<VkWriteDescriptorSet, 2> writes = { samplerWrite, imageWrite };
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	// We don't use this version
	void VulkanCubeMap::Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		auto& renderer = SwimEngine::GetInstance()->GetVulkanRenderer();
		VkCommandBuffer cmd = renderer->GetCommandManager()->GetCommandBuffers()[renderer->GetCurrentFrameIndex()];

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		glm::mat4 viewNoTrans = glm::mat4(glm::mat3(viewMatrix));

		struct PushData
		{
			glm::mat4 view;
			glm::mat4 proj;
		} push;

		push.view = viewNoTrans;
		push.proj = projectionMatrix;

		vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData), &push);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdDraw(cmd, 36, 1, 0, 0);
	}

	// We use this version
	void VulkanCubeMap::Render(VkCommandBuffer cmd, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		glm::mat4 viewNoTrans = glm::mat4(glm::mat3(viewMatrix));

		struct PushData
		{
			glm::mat4 view;
			glm::mat4 proj;
		} push;

		push.view = viewNoTrans;
		push.proj = projectionMatrix;

		vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData), &push);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdDraw(cmd, 36, 1, 0, 0);
	}

	std::vector<char> VulkanCubeMap::ReadFile(const std::string& filename)
	{
		std::string exeDir = SwimEngine::GetExecutableDirectory();
		std::string fullPath = exeDir + "\\" + filename;

		std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to load shader: " + fullPath);
		}

		size_t fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		std::cout << "[CubeMap] Loaded shader: " << fullPath << std::endl;

		return buffer;
	}

	VkShaderModule VulkanCubeMap::CreateShaderModule(const std::vector<char>& code) const
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create shader module!");
		}
		return shaderModule;
	}

	void VulkanCubeMap::CreatePipelineForSkybox()
	{
		// === Load SPIR-V shaders ===
		auto vertCode = ReadFile(vertShaderPath);
		auto fragCode = ReadFile(fragShaderPath);

		VkShaderModule vertModule = CreateShaderModule(vertCode);
		VkShaderModule fragModule = CreateShaderModule(fragCode);

		VkPipelineShaderStageCreateInfo vertStage{};
		vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertStage.module = vertModule;
		vertStage.pName = "main";

		VkPipelineShaderStageCreateInfo fragStage{};
		fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragStage.module = fragModule;
		fragStage.pName = "main";

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

		// === Minimal Vertex Input ===
		VkVertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(glm::vec3);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attr{};
		attr.binding = 0;
		attr.location = 0;
		attr.format = VK_FORMAT_R32G32B32_SFLOAT;
		attr.offset = 0;

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &binding;
		vertexInput.vertexAttributeDescriptionCount = 1;
		vertexInput.pVertexAttributeDescriptions = &attr;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_FRONT_BIT;
		raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo depth{};
		depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth.depthTestEnable = VK_TRUE;
		depth.depthWriteEnable = VK_FALSE;
		depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState blendAttachment{};
		blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo blend{};
		blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAttachment;

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(glm::mat4) * 2;

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &descriptorSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create skybox pipeline layout!");
		}

		VkRenderPass renderPass = SwimEngine::GetInstance()->GetVulkanRenderer()->GetPipelineManager()->GetRenderPass();

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &raster;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depth;
		pipelineInfo.pColorBlendState = &blend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) 
		{
			throw std::runtime_error("Failed to create skybox pipeline!");
		}

		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
	}

}
