#include "PCH.h"
#include "VulkanRenderer.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "VulkanCubeMap.h"

// Validation layers for debugging
#ifdef _DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

namespace Engine
{

	static constexpr unsigned int RoundUpToNextPowerOfTwo(unsigned int x)
	{
		if (x == 0) { return 1; }

		// If already a power of two, return as-is
		if ((x & (x - 1)) == 0)
		{
			return x;
		}

		// Round up to next power of two
		--x;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		return x + 1;
	}

	void VulkanRenderer::Create(HWND hwnd, uint32_t width, uint32_t height)
	{
		windowWidth = width;
		windowHeight = height;
		windowHandle = hwnd;

		if (!windowHandle)
		{
			throw std::runtime_error("Invalid window handle passed to VulkanRenderer.");
		}
	}

	// Create Vulkan components
	int VulkanRenderer::Awake()
	{
		// Ctor does full creation
		deviceManager = std::make_unique<VulkanDeviceManager>(
			windowHandle,
			windowWidth,
			windowHeight
		);

		VkDevice device = deviceManager->GetDevice();
		VkPhysicalDevice physicalDevice = deviceManager->GetPhysicalDevice();
		msaaSamples = deviceManager->GetMaxUsableSampleCount(); 
		if (msaaSamples > VK_SAMPLE_COUNT_4_BIT)
		{
			msaaSamples = VK_SAMPLE_COUNT_4_BIT; // 4x msaa is fine as max for now
		}

		// Ctor inits phase 1 for some image formats needed for the render pass
		swapChainManager = std::make_unique<VulkanSwapChain>(
			physicalDevice,
			device,
			deviceManager->GetSurface(),
			windowWidth,
			windowHeight
		);

		// Make the pipeline which we then make the render pass with
		pipelineManager = std::make_unique<VulkanPipelineManager>(
			device
		);

		pipelineManager->CreateRenderPass(swapChainManager->GetPendingImageFormat(), swapChainManager->GetPendingDepthFormat(), msaaSamples);
		VkRenderPass renderPass = pipelineManager->GetRenderPass();

		swapChainManager->Create(renderPass, msaaSamples); // phase 2 of swapchain creation

		// We need the texture pool for getting how many textures we will need in our bindless textures array.
		// After all this Vulkan initing, we can then load all textures.
		Engine::TexturePool& texturePool = TexturePool::GetInstance();

		texturePool.FetchTextureCount(); // Counts image files to load in assets (caches it).
		unsigned int texCount = texturePool.GetTextureCount();
		unsigned int maxBindlessTextureCount = RoundUpToNextPowerOfTwo(texCount) * 2; // double in expected size for dynamic texture and engine stuff that could happen

		constexpr uint32_t MAX_SETS = 1024;
		constexpr uint64_t SSBO_SIZE = 10240;

		// Make the descriptor manager, its ctor creates the layout and pool
		descriptorManager = std::make_unique<VulkanDescriptorManager>(
			device,
			MAX_SETS,
			maxBindlessTextureCount,
			SSBO_SIZE
		);

		// Create bindless descriptor layout and set
		descriptorManager->CreateBindlessLayout();
		descriptorManager->CreateBindlessPool();
		descriptorManager->AllocateBindlessSet();

		// Make the default sampler for the fragment shader to use
		defaultSampler = CreateSampler();
		descriptorManager->SetBindlessSampler(defaultSampler);

		// Set up buffer and UBO for camera with double buffering
		descriptorManager->CreatePerFrameUBOs(physicalDevice, MAX_FRAMES_IN_FLIGHT);

		constexpr int MAX_EXPECTED_INSTANCES = 10240;

		// Create the index draw object which stores our instanced buffers and does our indexed drawing logic and caching
		indexDraw = std::make_unique<VulkanIndexDraw>(
			device,
			physicalDevice,
			MAX_EXPECTED_INSTANCES,
			MAX_FRAMES_IN_FLIGHT
		);
		indexDraw->CreateIndirectBuffers(MAX_EXPECTED_INSTANCES, MAX_FRAMES_IN_FLIGHT);

		// We have a huge buffer on the GPU now to store all of our meshes so we never have to change vertice and indice bindings
		constexpr VkDeviceSize initialVertexSize = 16 * 1024 * 1024; // 16 MB
		constexpr VkDeviceSize initialIndexSize = 4 * 1024 * 1024;  // 4 MB

		indexDraw->CreateMegaMeshBuffers(initialVertexSize, initialIndexSize);

		// Configure culled rendering mode
		// Debug mode CPU culling: 100 FPS 
		// Release mode CPU culling: 2500+ FPS
		// GPU compute shader culling is not implemented
		indexDraw->SetCulledMode(VulkanIndexDraw::CullMode::CPU);
		indexDraw->SetUseQueriedFrustumSceneBVH(true);

		// Hook the index buffer SSBO into our per-frame descriptor sets
		descriptorManager->CreateInstanceBufferDescriptorSets(indexDraw->GetInstanceBuffer()->GetPerFrameBuffers());

		// === Graphics pipeline creation ===
		VkDescriptorSetLayout layout = descriptorManager->GetLayout();
		VkDescriptorSetLayout bindlessLayout = descriptorManager->GetBindlessLayout();

		auto vertexAttribs = Vertex::GetAttributeDescriptions();
		auto instanceAttribs = Vertex::GetInstanceAttributeDescriptions();

		std::vector<VkVertexInputAttributeDescription> allAttribs;
		allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
		allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

		std::array<VkVertexInputBindingDescription, 2> bindings{};
		bindings[0] = Vertex::GetBindingDescription();
		bindings[1].binding = 1;
		bindings[1].stride = sizeof(GpuInstanceData);
		bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		pipelineManager->CreateGraphicsPipeline(
			"Shaders\\VertexShaders\\vertex_instanced.spv",
			"Shaders\\FragmentShaders\\fragment_instanced.spv",
			layout,
			bindlessLayout,
			std::vector<VkVertexInputBindingDescription>{ bindings.begin(), bindings.end() },
			allAttribs,
			sizeof(GpuInstanceData)
		);

		pipelineManager->CreateUIPipeline(
			"Shaders\\VertexShaders\\vertex_ui.spv",
			"Shaders\\FragmentShaders\\fragment_ui.spv",
			layout,
			bindlessLayout,
			{ bindings.begin(), bindings.end() },
			allAttribs,
			sizeof(UIParams)
		);

		// Initialize command manager with correct graphics queue family index
		uint32_t graphicsQueueFamilyIndex = deviceManager->FindQueueFamilies(physicalDevice).graphicsFamily.value();
		commandManager = std::make_unique<VulkanCommandManager>(
			device,
			graphicsQueueFamilyIndex
		);

		commandManager->AllocateCommandBuffers(static_cast<uint32_t>(swapChainManager->GetFramebuffers().size()));

		// Fencing and sync
		syncManager = std::make_unique<VulkanSyncManager>(
			device,
			MAX_FRAMES_IN_FLIGHT
		);

		// Load all textures and set a fallback missing texture
		texturePool.LoadAllRecursively();
		missingTexture = texturePool.GetTexture2DLazy("mart");

		// Now set up the cubemap
		cubemapController = std::make_unique<CubeMapController>(
			"Shaders\\VertexShaders\\vertex_cubemap.spv",
			"Shaders\\FragmentShaders\\fragment_cubemap.spv"
		);
		cubemapController->SetEnabled(false);

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
			swapChainManager->Recreate(windowWidth, windowHeight, pipelineManager->GetRenderPass(), msaaSamples);

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

	// Clean up Vulkan resources (a lot of these things deconstructors call CleanUp() for them via reset() but we manually call it anyways)
	int VulkanRenderer::Exit()
	{
		auto device = deviceManager->GetDevice();

		vkDeviceWaitIdle(device);

		if (cubemapController)
		{
			cubemapController.reset(); 
		}

		swapChainManager->Cleanup();
		swapChainManager.reset();

		MeshPool::GetInstance().Flush();
		TexturePool::GetInstance().Flush();
		missingTexture.reset();

		syncManager->Cleanup();
		syncManager.reset();

		if (defaultSampler)
		{
			vkDestroySampler(device, defaultSampler, nullptr);
		}

		indexDraw->CleanUp();
		indexDraw.reset();

		pipelineManager->Cleanup();
		pipelineManager.reset();

		descriptorManager->Cleanup();
		descriptorManager.reset();

		commandManager->Cleanup();
		commandManager.reset();

		deviceManager->Cleanup();
		// Resetting the device manager causes a crash because some resources like textures still need to be released and that requires using the device.
		// This is weird because we call Flush() on the texture pool first.
		// deviceManager.reset();

		return 0;
	}

	void VulkanRenderer::DrawFrame()
	{
		// wait for gpu to be ready for this frame
		syncManager->WaitForFence(currentFrame);
		syncManager->ResetFence(currentFrame);
		VkSemaphore imageAvailableSemaphore = syncManager->GetImageAvailableSemaphore(currentFrame);

		// get the next image from the swapchain for this frame
		uint32_t imageIndex;
		VkResult result = swapChainManager->AcquireNextImage(imageAvailableSemaphore, &imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
		{
			framebufferResized = false;
			return;
		}
		else if (result != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to acquire swap chain image!");
		}

		// this method draws everything for the frame (recording draw commands)
		RecordCommandBuffer(imageIndex);

		// get a bunch of stuff ready for submission for this frame to queue up and present everything we just recorded

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		const std::vector<VkCommandBuffer>& commandBuffers = commandManager->GetCommandBuffers();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

		VkSemaphore renderFinishedSemaphore = syncManager->GetRenderFinishedSemaphore(currentFrame);

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		VkFence inFlightFence = syncManager->GetInFlightFence(currentFrame);

		// submit and present everything

		if (vkQueueSubmit(deviceManager->GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit draw command buffer!");
		}

		result = swapChainManager->Present(deviceManager->GetPresentQueue(), signalSemaphores, imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
		{
			framebufferResized = false;
		}
		else if (result != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to present swap chain image!");
		}

		// cycle forward (double buffering frames in flight)
		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void VulkanRenderer::UpdateUniformBuffer()
	{
		CameraUBO ubo{};

		ubo.view = cameraSystem->GetViewMatrix();
		ubo.proj = cameraSystem->GetProjectionMatrix();  // already has the Vulkan Y-flip

		const auto& camera = cameraSystem->GetCamera();

		// Calculate half FOV tangents - make sure signs are correct
		float tanHalfFovY = tan(glm::radians(camera.GetFOV() * 0.5f));
		float tanHalfFovX = tanHalfFovY * camera.GetAspect();

		ubo.camParams = glm::vec4(
			tanHalfFovX,
			tanHalfFovY,
			camera.GetNearClip(),
			camera.GetFarClip()
		);

		ubo.screenView = glm::mat4(1.0f); // Identity

		ubo.screenProj = glm::ortho(
			0.0f, VirtualCanvasWidth,
			VirtualCanvasHeight, 0.0f, // Flip Y for Vulkan
			-1.0f, 1.0f
		);

		descriptorManager->UpdatePerFrameUBO(currentFrame, ubo);
	}

	void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex)
	{
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		const std::vector<VkCommandBuffer>& commandBuffers = commandManager->GetCommandBuffers();
		VkCommandBuffer cmd = commandBuffers[imageIndex];

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to begin recording command buffer!");
		}

		// Update camera UBO and instance buffer
		UpdateUniformBuffer();
		indexDraw->UpdateInstanceBuffer(currentFrame);

		// Begin render pass
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };

		const std::vector<VkFramebuffer>& framebuffers = swapChainManager->GetFramebuffers();
		VkExtent2D extent = swapChainManager->GetExtent();

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = pipelineManager->GetRenderPass();
		renderPassInfo.framebuffer = framebuffers[imageIndex];
		renderPassInfo.renderArea = { {0, 0}, extent };
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Dynamic viewport and scissor
		VkViewport viewport{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
		VkRect2D scissor{ {0, 0}, extent };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// === SKYBOX: Draw first using skybox pipeline ===
		if (cubemapController && cubemapController->IsEnabled())
		{
			CubeMap* map = cubemapController->GetCubeMap();
			VulkanCubeMap* vkMap = static_cast<VulkanCubeMap*>(map);
			if (vkMap)
			{
				vkMap->Render(cmd, cameraSystem->GetViewMatrix(), cameraSystem->GetProjectionMatrix());
			}
		}

		// === Rebind original scene pipeline and descriptor sets ===
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineManager->GetGraphicsPipeline());

		VkDescriptorSet globalSet = descriptorManager->GetPerFrameDescriptorSet(currentFrame);
		VkDescriptorSet bindlessSet = descriptorManager->GetBindlessSet();

		std::array<VkDescriptorSet, 2> sets = { globalSet, bindlessSet };

		vkCmdBindDescriptorSets(
			cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineManager->GetPipelineLayout(),
			0,
			static_cast<uint32_t>(sets.size()),
			sets.data(),
			0,
			nullptr
		);

		// === Scene: Draw all indexed meshes ===
		indexDraw->DrawIndexedWorldSpace(currentFrame, cmd);
		indexDraw->DrawIndexedScreenSpaceUI(currentFrame, cmd);

		vkCmdEndRenderPass(cmd);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to record command buffer!");
		}
	}

	//////////////////////////////////
	//				HELPERS BELOW					//
	//////////////////////////////////

	void VulkanRenderer::SetSurfaceSize(uint32_t newWidth, uint32_t newHeight)
	{
		windowWidth = newWidth;
		windowHeight = newHeight;
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

	void VulkanRenderer::CopyBuffer(
		VkBuffer srcBuffer,
		VkBuffer dstBuffer,
		VkDeviceSize size
	) const
	{
		VkCommandBuffer commandBuffer = commandManager->BeginSingleTimeCommands();

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

		auto graphicsQueue = deviceManager->GetGraphicsQueue();

		commandManager->EndSingleTimeCommands(commandBuffer, graphicsQueue);
	}

	void VulkanRenderer::CopyBuffer(
		VkBuffer srcBuffer,
		VkBuffer dstBuffer,
		VkDeviceSize size,
		VkDeviceSize dstOffset
	) const
	{
		VkCommandBuffer commandBuffer = commandManager->BeginSingleTimeCommands();

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = dstOffset;
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

		auto graphicsQueue = deviceManager->GetGraphicsQueue();
		commandManager->EndSingleTimeCommands(commandBuffer, graphicsQueue);
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
		VkCommandBuffer commandBuffer = commandManager->BeginSingleTimeCommands();

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

		auto graphicsQueue = deviceManager->GetGraphicsQueue();

		commandManager->EndSingleTimeCommands(commandBuffer, graphicsQueue);
	}

	void VulkanRenderer::CopyBufferToImage(
		VkBuffer buffer,
		VkImage image,
		uint32_t width,
		uint32_t height
	)
	{
		VkCommandBuffer commandBuffer = commandManager->BeginSingleTimeCommands();

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

		auto graphicsQueue = deviceManager->GetGraphicsQueue();

		commandManager->EndSingleTimeCommands(commandBuffer, graphicsQueue);
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

	// we should abstract this in the future if we need more than one sampler ever
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

}
