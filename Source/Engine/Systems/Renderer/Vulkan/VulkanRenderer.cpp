#include "PCH.h"
#include "VulkanRenderer.h"
#include "Engine/SwimEngine.h"

#include <set>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <fstream>
#include <filesystem>

#include "Engine/Systems/Renderer/Core/Meshes/Vertex.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"
#include "DescriptorPool.h"

#include "Library/glm/gtc/matrix_transform.hpp"

// Validation layers for debugging
#ifdef _DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

namespace Engine
{

	std::vector<const char*> VulkanRenderer::validationLayers = {
			"VK_LAYER_KHRONOS_validation"
	};

	std::vector<char> VulkanRenderer::ReadFile(const std::string& filename)
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

		std::cout << "Successfully loaded shader: " << fullPath << std::endl;

		return buffer;
	}

	// Create Vulkan components
	int VulkanRenderer::Awake()
	{
		// ctor does full creation
		deviceManager = std::make_shared<VulkanDeviceManager>(
			windowHandle,
			windowWidth,
			windowHeight,
			validationLayers,
			deviceExtensions
		);

		// ctor inits some image formats needed for the render pass
		swapChainManager = std::make_shared<VulkanSwapChain>(
			deviceManager->GetPhysicalDevice(),
			deviceManager->GetDevice(),
			deviceManager->GetSurface(),
			windowWidth,
			windowHeight
		);

		CreateRenderPass(); // Will remain in VulkanRenderer for now

		swapChainManager->Create(renderPass); // phase 2 of swapchain creation

		CreateDescriptorSetLayout();
		CreateDescriptorPool();
		CreatePipelineLayout();
		CreateGraphicsPipeline();

		CreateCommandPool();
		AllocateCommandBuffers();
		CreateSyncObjects();

		uniformBuffer = std::make_unique<VulkanBuffer>(
			deviceManager->GetDevice(),
			deviceManager->GetPhysicalDevice(),
			sizeof(CameraUBO),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		TexturePool::GetInstance().LoadAllRecursively();
		missingTexture = TexturePool::GetInstance().GetTexture2DLazy("mart");
		defaultSampler = CreateSampler();

		return 0;
	}

	// Called when system initializes
	int VulkanRenderer::Init()
	{
		// Get the camera system
		cameraSystem = SwimEngine::GetInstance()->GetCameraSystem();

		return 0;
	}

	// Called every frame
	void VulkanRenderer::Update(double dt)
	{
		// If minimized, do NOT call DrawFrame or we will be deadlocked on a null sized surface
		if (windowWidth == 0 || windowHeight == 0)
		{
			// later on we might still want to do some logic for GPU compute things even with the window minimized. We might even want our own system for that.
			return;
		}

		// Check if we need to resize and recreate stuff. If we do, then afterwards skip drawing this frame to avoid potential synchronization weirdness
		if (framebufferResized && cameraSystem.get() != nullptr)
		{
			framebufferResized = false;
			swapChainManager->Recreate(windowWidth, windowHeight, renderPass);

			// Then refresh the camera systems aspect ratio to the new windows size
			// This should be the main engine classes job to call this on window resize finish, but it just works best here
			cameraSystem->RefreshAspect();
			return;
		}

		// Render the frame
		DrawFrame();
	}

	// Called every fixed tick
	void VulkanRenderer::FixedUpdate(unsigned int tickThisSecond)
	{
		// For physics related fixed steps, probably not going to be used here for a long time unless we have some complex gpu driven particle systems
	}

	// Clean up Vulkan resources
	int VulkanRenderer::Exit()
	{
		vkDeviceWaitIdle(deviceManager->GetDevice());

		swapChainManager->Cleanup();

		MeshPool::GetInstance().Flush();
		TexturePool::GetInstance().Flush();
		missingTexture.reset();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroySemaphore(deviceManager->GetDevice(), renderFinishedSemaphores[i], nullptr);
			vkDestroySemaphore(deviceManager->GetDevice(), imageAvailableSemaphores[i], nullptr);
			vkDestroyFence(deviceManager->GetDevice(), inFlightFences[i], nullptr);
		}

		if (defaultSampler)
		{
			vkDestroySampler(deviceManager->GetDevice(), defaultSampler, nullptr);
		}

		vkDestroyPipeline(deviceManager->GetDevice(), graphicsPipeline, nullptr);
		vkDestroyPipelineLayout(deviceManager->GetDevice(), pipelineLayout, nullptr);
		vkDestroyRenderPass(deviceManager->GetDevice(), renderPass, nullptr);

		vkDestroyDescriptorSetLayout(deviceManager->GetDevice(), descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(deviceManager->GetDevice(), descriptorPool, nullptr);

		uniformBuffer.reset();

		vkDestroyCommandPool(deviceManager->GetDevice(), commandPool, nullptr);

		deviceManager->Cleanup();

		return 0;
	}

	void VulkanRenderer::DrawFrame()
	{
		if (vkWaitForFences(deviceManager->GetDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to wait for in-flight fence!");
		}

		if (vkResetFences(deviceManager->GetDevice(), 1, &inFlightFences[currentFrame]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to reset in-flight fence!");
		}

		uint32_t imageIndex;
		// not a method
		VkResult result = swapChainManager->AcquireNextImage(imageAvailableSemaphores[currentFrame], &imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
		{
			framebufferResized = false;
			return;
		}
		else if (result != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to acquire swap chain image!");
		}

		RecordCommandBuffer(imageIndex);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(deviceManager->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit draw command buffer!");
		}

		// not a method
		result = swapChainManager->Present(deviceManager->GetPresentQueue(), signalSemaphores, imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
		{
			framebufferResized = false;
		}
		else if (result != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to present swap chain image!");
		}

		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	VkDescriptorSet VulkanRenderer::CreateDescriptorSet(const std::shared_ptr<Texture2D>& texture) const
	{
		VkDescriptorSetLayout layouts[] = { descriptorSetLayout };

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = layouts;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(deviceManager->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate descriptor set for material!");
		}

		// Bind UBO (binding 0)
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = uniformBuffer->GetBuffer();
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(CameraUBO);

		VkWriteDescriptorSet uboWrite{};
		uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		uboWrite.dstSet = descriptorSet;
		uboWrite.dstBinding = 0;
		uboWrite.dstArrayElement = 0;
		uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboWrite.descriptorCount = 1;
		uboWrite.pBufferInfo = &bufferInfo;

		// Bind texture sampler (binding 1)
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = defaultSampler;
		imageInfo.imageView = texture ? texture->GetImageView() : missingTexture->GetImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet samplerWrite{};
		samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		samplerWrite.dstSet = descriptorSet;
		samplerWrite.dstBinding = 1;
		samplerWrite.dstArrayElement = 0;
		samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerWrite.descriptorCount = 1;
		samplerWrite.pImageInfo = &imageInfo;

		std::array<VkWriteDescriptorSet, 2> writes = { uboWrite, samplerWrite };

		vkUpdateDescriptorSets(deviceManager->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		return descriptorSet;
	}

	void VulkanRenderer::SetSurfaceSize(uint32_t newWidth, uint32_t newHeight)
	{
		windowWidth = newWidth;
		windowHeight = newHeight;
	}

	VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() const
	{
		// Allocate a temporary command buffer from commandPool
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool;  // your existing commandPool
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		if (vkAllocateCommandBuffers(deviceManager->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate single-time command buffer!");
		}

		// Begin recording
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
	{
		vkEndCommandBuffer(commandBuffer);

		// Submit once to the graphics queue
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		auto graphicsQueue = deviceManager->GetGraphicsQueue();

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit single-time command buffer!");
		}

		vkQueueWaitIdle(graphicsQueue);

		vkFreeCommandBuffers(deviceManager->GetDevice(), commandPool, 1, &commandBuffer);
	}

	void VulkanRenderer::CreateBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkBuffer& buffer,
		VkDeviceMemory& bufferMemory
	) const
	{
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		auto device = deviceManager->GetDevice();

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create buffer!");
		}

		// Query memory requirements
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

		// Allocate
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = swapChainManager->FindMemoryType(memRequirements.memoryTypeBits, properties);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate buffer memory!");
		}

		vkBindBufferMemory(device, buffer, bufferMemory, 0);
	}

	void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const
	{
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

		EndSingleTimeCommands(commandBuffer);
	}

	void VulkanRenderer::CreateImage(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkImage& outImage,
		VkDeviceMemory& outImageMemory
	)
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		auto device = deviceManager->GetDevice();

		if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create image!");
		}

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, outImage, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = swapChainManager->FindMemoryType(
			memRequirements.memoryTypeBits,
			properties
		);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &outImageMemory) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate image memory!");
		}

		vkBindImageMemory(device, outImage, outImageMemory, 0);
	}

	void VulkanRenderer::TransitionImageLayout(
		VkImage image,
		VkFormat format,
		VkImageLayout oldLayout,
		VkImageLayout newLayout
	)
	{
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		// Determine source/dest stage masks
		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else
		{
			// For simplicity, handle only these two transitions
			throw std::runtime_error("Unsupported layout transition!");
		}

		vkCmdPipelineBarrier(
			commandBuffer,
			sourceStage, destinationStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		EndSingleTimeCommands(commandBuffer);
	}

	void VulkanRenderer::CopyBufferToImage(
		VkBuffer buffer,
		VkImage image,
		uint32_t width,
		uint32_t height
	)
	{
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		vkCmdCopyBufferToImage(
			commandBuffer,
			buffer,
			image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region
		);

		EndSingleTimeCommands(commandBuffer);
	}

	VkImageView VulkanRenderer::CreateImageView(
		VkImage image,
		VkFormat format
	)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView imageView;
		if (vkCreateImageView(deviceManager->GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create texture image view!");
		}

		return imageView;
	}

	VkSampler VulkanRenderer::CreateSampler()
	{
		// TODO: this is the most basic sampler that doesn't do any tricks like LOD or mip mapping. We will defintely want and need that later on.
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		// Check if anisotropy is supported and enabled
		VkPhysicalDeviceFeatures supportedFeatures;
		vkGetPhysicalDeviceFeatures(deviceManager->GetPhysicalDevice(), &supportedFeatures);

		if (supportedFeatures.samplerAnisotropy)
		{
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = 16.0f; // TODO: Clamp this value to physical device limits
		}
		else
		{
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = 1.0f;
		}

		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		VkSampler newSampler;
		if (vkCreateSampler(deviceManager->GetDevice(), &samplerInfo, nullptr, &newSampler) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create texture sampler!");
		}

		return newSampler;
	}

	// --------------------------------------------------------------------------------------
	// Vulkan setup functions
	// --------------------------------------------------------------------------------------

	void VulkanRenderer::CreateRenderPass()
	{
		// --- Color attachment ---
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = swapChainManager->GetPendingImageFormat();
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// --- Depth attachment ---
		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = swapChainManager->GetPendingDepthFormat();
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentRef{};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Subpass references
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		// We add the depth attachment here
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		// For proper synchronization between the subpass and external usage
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		// Our 2 attatchments are color and depth
		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(deviceManager->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create render pass!");
		}
	}

	void VulkanRenderer::CreatePipelineLayout()
	{
		// We define a single push constant range that covers our PushConstantData struct
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PushConstantData);

		// We already have descriptorSetLayout (2 bindings: UBO + sampler).
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; // references that single set
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(deviceManager->GetDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create pipeline layout!");
		}
	}

	void VulkanRenderer::CreateDescriptorPool()
	{
		// Let's assume up to 100 materials for now (this will almost certainly change and need to be in the thousands)
		constexpr uint32_t MAX_MATERIALS = 100; // why is this not an alligned value like 2048?

		// We have 2 descriptor types: UBO and combined sampler
		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = MAX_MATERIALS;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = MAX_MATERIALS;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		// We allow up to MAX_MATERIALS distinct sets
		poolInfo.maxSets = MAX_MATERIALS;

		if (vkCreateDescriptorPool(deviceManager->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create descriptor pool!");
		}
	}

	void VulkanRenderer::CreateGraphicsPipeline()
	{
		// Load compiled SPIR-V code for vertex and fragment shaders
		auto vertShaderCode = ReadFile("Shaders\\VertexShaders\\vertex.spv");
		auto fragShaderCode = ReadFile("Shaders\\FragmentShaders\\fragment.spv");

		VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
		VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

		// Prepare the pipeline stages
		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		// Vertex input state
		auto bindingDescription = Vertex::GetBindingDescription();
		auto attributeDescriptions = Vertex::GetAttributeDescriptions();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Viewport/scissor are set dynamically
		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// Dynamic states
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		// Rasterizer
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		// Multisampling (not used here, just pass defaults)
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Depth-stencil state
		// We enable depth testing so that fragments are properly depth-tested.
		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		// Color blending
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		// Build the pipeline with our layout (which includes descriptor sets and push constants)
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		// Create the graphics pipeline
		auto device = deviceManager->GetDevice();
		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create graphics pipeline!");
		}

		// Cleanup shader modules
		vkDestroyShaderModule(device, vertShaderModule, nullptr);
		vkDestroyShaderModule(device, fragShaderModule, nullptr);
	}

	void VulkanRenderer::CreateCommandPool()
	{
		QueueFamilyIndices queueFamilyIndices = deviceManager->FindQueueFamilies(deviceManager->GetPhysicalDevice());

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
		// Enable the reset flag to allow individual command buffer resets
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		if (vkCreateCommandPool(deviceManager->GetDevice(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create command pool!");
		}
	}

	// Allocate command buffers (separated from creation)
	void VulkanRenderer::AllocateCommandBuffers()
	{
		commandBuffers.resize(swapChainManager->GetFramebuffers().size());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

		if (vkAllocateCommandBuffers(deviceManager->GetDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate command buffers!");
		}
	}

	void VulkanRenderer::CreateSyncObjects()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Ensures that the first frame can be rendered immediately

		imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

		auto device = deviceManager->GetDevice();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create semaphores!");
			}

			if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create fences!");
			}
		}
	}

	void VulkanRenderer::CreateDescriptorSetLayout()
	{
		// Binding 0: Uniform buffer (camera UBO)
		VkDescriptorSetLayoutBinding uboLayoutBinding{};
		uboLayoutBinding.binding = 0;
		uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboLayoutBinding.descriptorCount = 1;
		uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Used in vertex

		// Binding 1: Combined image sampler (for texture)
		VkDescriptorSetLayoutBinding samplerLayoutBinding{};
		samplerLayoutBinding.binding = 1;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.descriptorCount = 1;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // Used in fragment
		samplerLayoutBinding.pImmutableSamplers = nullptr; // We'll use a custom sampler

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
				uboLayoutBinding,
				samplerLayoutBinding
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(deviceManager->GetDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create descriptor set layout!");
		}
	}

	void VulkanRenderer::UpdateUniformBuffer()
	{
		CameraUBO ubo{};
		ubo.view = cameraSystem->GetViewMatrix();
		ubo.proj = cameraSystem->GetProjectionMatrix();

		uniformBuffer->CopyData(&ubo, sizeof(CameraUBO));
	}

	// --------------------------------------------------------------------------------------
	// Helper methods
	// --------------------------------------------------------------------------------------

	std::vector<const char*> VulkanRenderer::GetRequiredExtensions() const
	{
		// On Windows + no GLFW: 
		std::vector<const char*> extensions = {
				"VK_KHR_surface",
				"VK_KHR_win32_surface"
		};
		if (enableValidationLayers)
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extensions;
	}

	VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) const
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		// Ensure that code.size() is a multiple of 4
		if (code.size() % 4 != 0)
		{
			throw std::runtime_error("Shader code size is not a multiple of 4!");
		}
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(deviceManager->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create shader module!");
		}

		return shaderModule;
	}

	void VulkanRenderer::UpdateMaterialDescriptorSet(VkDescriptorSet dstSet, const Material& mat) const
	{
		// If there's no texture, skip or bind a dummy
		if (!mat.data->albedoMap)
		{
			return; // no texture to bind
		}

		// Prepare the descriptor image info
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = defaultSampler; // we have a single default sampler
		imageInfo.imageView = mat.data->albedoMap->GetImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Write to binding=1 of the descriptor set
		VkWriteDescriptorSet samplerWrite{};
		samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		samplerWrite.dstSet = dstSet;
		samplerWrite.dstBinding = 1;
		samplerWrite.dstArrayElement = 0;
		samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerWrite.descriptorCount = 1;
		samplerWrite.pImageInfo = &imageInfo;

		// Perform the update
		vkUpdateDescriptorSets(deviceManager->GetDevice(), 1, &samplerWrite, 0, nullptr);
	}

	inline MeshBufferData& VulkanRenderer::GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh)
	{
		// Check if the Mesh already has its MeshBufferData initialized, it pretty much always should due to MeshPool::RegisterMesh()
		if (mesh->meshBufferData)
		{
			return *mesh->meshBufferData;
		}
		// Below is back up code:

		// Create a new MeshBufferData instance
		auto data = std::make_shared<MeshBufferData>();

		// Generate GPU buffers (Vulkan version requires device + physicalDevice)
		data->GenerateBuffers(mesh->vertices, mesh->indices, deviceManager->GetDevice(), deviceManager->GetPhysicalDevice());

		// Store the newly created MeshBufferData in the Mesh
		mesh->meshBufferData = data;

		return *data;
	}

	void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex)
	{
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to begin recording command buffer!");
		}

		// Clear color + depth
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };

		auto swapChainFramebuffers = swapChainManager->GetFramebuffers();
		auto swapChainExtent = swapChainManager->GetExtent();

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Set dynamic viewport
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapChainExtent.width);
		viewport.height = static_cast<float>(swapChainExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &viewport);

		// Set dynamic scissor
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = swapChainExtent;
		vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &scissor);

		// Bind pipeline
		vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

		// Update camera UBO
		UpdateUniformBuffer();

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		if (scene)
		{
			auto& registry = scene->GetRegistry();
			auto view = registry.view<Transform, Material>();

			for (auto entity : view)
			{
				auto& transform = view.get<Transform>(entity);
				auto& mat = view.get<Material>(entity).data;

				// Ensure GPU buffers for mesh
				auto& meshData = GetOrCreateMeshBuffers(mat->mesh);

				// 1) Ensure VulkanDescriptor is initialized
				if (!mat->vulkanDescriptor)
				{
					mat->vulkanDescriptor = DescriptorPool::GetInstance().GetDescriptor(*this, mat->albedoMap);
				}

				// 2) Push constants (model matrix + hasTexture)
				PushConstantData pcData{};
				pcData.model = transform.GetModelMatrix();
				pcData.hasTexture = (mat->albedoMap ? 1.0f : 0.0f);
				pcData.padA = pcData.padB = pcData.padC = 0.0f;

				vkCmdPushConstants(commandBuffers[imageIndex],
					pipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0,
					sizeof(PushConstantData),
					&pcData);

				// 3) Bind the Material's descriptor set
				vkCmdBindDescriptorSets(
					commandBuffers[imageIndex],
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipelineLayout,
					0, 1, &mat->vulkanDescriptor->descriptorSet,
					0, nullptr
				);

				// 4) Bind vertex + index buffers
				VkBuffer vertexBuffers[] = { meshData.vertexBuffer->GetBuffer() };
				VkDeviceSize offsets[] = { 0 };
				vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(commandBuffers[imageIndex], meshData.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

				// 5) Draw
				vkCmdDrawIndexed(commandBuffers[imageIndex], meshData.indexCount, 1, 0, 0, 0);
			}
		}

		vkCmdEndRenderPass(commandBuffers[imageIndex]);

		if (vkEndCommandBuffer(commandBuffers[imageIndex]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to record command buffer!");
		}
	}

}
