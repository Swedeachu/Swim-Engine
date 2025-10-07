#include "PCH.h"
#include "VulkanRenderer.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Font/FontPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Components/Transform.h"
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

		// texturePool.FetchTextureCount(); // Counts image files to load in assets (caches it).
		// unsigned int texCount = texturePool.GetTextureCount();
		// unsigned int maxBindlessTextureCount = RoundUpToNextPowerOfTwo(texCount) * 2; // double in expected size for dynamic texture and engine stuff that could happen
		unsigned int maxBindlessTextureCount = 4096; // we need this big since we make a lot of textures on the fly from memory

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
		VkDescriptorSetLayout layout = descriptorManager->GetLayout(); // set 0
		VkDescriptorSetLayout bindlessLayout = descriptorManager->GetBindlessLayout(); // set 1

		auto vertexAttribs = Vertex::GetAttributeDescriptions();
		auto instanceAttribs = Vertex::GetInstanceAttributeDescriptions();

		std::vector<VkVertexInputAttributeDescription> allAttribs;
		allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
		allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

		std::array<VkVertexInputBindingDescription, 2> meshBindings{};
		meshBindings[0] = Vertex::GetBindingDescription(); // mesh VB
		meshBindings[1].binding = 1; // instance data
		meshBindings[1].stride = sizeof(GpuInstanceData);
		meshBindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		// ---- REGULAR MESH PIPELINE ----
		pipelineManager->CreateGraphicsPipeline(
			"Shaders\\VertexShaders\\vertex_instanced.spv",
			"Shaders\\FragmentShaders\\fragment_instanced.spv",
			layout,
			bindlessLayout,
			std::vector<VkVertexInputBindingDescription>{ meshBindings.begin(), meshBindings.end() },
			allAttribs,
			sizeof(GpuInstanceData)
		);

		// ---- DECORATED/UI PIPELINE ----
		pipelineManager->CreateDecoratedMeshPipeline(
			"Shaders\\VertexShaders\\vertex_decorated.spv",
			"Shaders\\FragmentShaders\\fragment_decorated.spv",
			layout,
			bindlessLayout,
			{ meshBindings.begin(), meshBindings.end() },
			allAttribs,
			sizeof(MeshDecoratorGpuInstanceData)
		);

		// ---- MSDF TEXT PIPELINE: own minimal bindings/attribs ----
		std::vector<VkVertexInputBindingDescription> msdfBindings = {
				{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
		};

		std::vector<VkVertexInputAttributeDescription> msdfAttribs = {
				{ 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position) }
		};

		pipelineManager->CreateMsdfTextPipeline(
			"Shaders\\VertexShaders\\vertex_msdf.spv",
			"Shaders\\FragmentShaders\\fragment_msdf.spv",
			layout,
			bindlessLayout,
			msdfBindings,
			msdfAttribs,
			sizeof(MsdfTextGpuInstanceData)
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
		// In the future we won't do this because the active scene file assets should determine which textures and models get loaded in, everything being loaded like this is just temporary behavior.
		// We will have a proper asset streaming threaded service later on.
		texturePool.LoadAllRecursively();
		missingTexture = texturePool.GetTexture2DLazy("mart");

		// Now set up the cubemap
		cubemapController = std::make_unique<CubeMapController>(
			"Shaders\\VertexShaders\\vertex_cubemap.spv",
			"Shaders\\FragmentShaders\\fragment_cubemap.spv"
		);
		cubemapController->SetEnabled(false);

		// Load all fonts (later on will not be done here and instead be done via threaded asset streaming service on demand)
		FontPool::GetInstance().LoadAllRecursively();

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
			return;
		}

		// Handle requested swapchain recreate (from present SUBOPTIMAL/OUT_OF_DATE) or window resize
		if ((framebufferResized || needsSwapchainRecreate) && cameraSystem.get() != nullptr)
		{
			framebufferResized = false;
			needsSwapchainRecreate = false;

			swapChainManager->Recreate(windowWidth, windowHeight, pipelineManager->GetRenderPass(), msaaSamples);

			// If swapchain images changed, reset imagesInFlight to match new count
			const auto& fbs = swapChainManager->GetFramebuffers();
			imagesInFlight.assign(fbs.size(), VK_NULL_HANDLE);

			cameraSystem->RefreshAspect();
			return;
		}

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
		MaterialPool::GetInstance().Flush();
		missingTexture.reset();
		Texture2D::FlushAllTextures(); // Free the straggler textures that were procedurally generated in memory for the GPU

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
		deviceManager.reset();

		return 0;
	}

	void VulkanRenderer::UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData)
	{
		if (indexDraw) indexDraw->UploadMeshToMegaBuffer(vertices, indices, meshData);
	}

	void VulkanRenderer::DrawFrame()
	{
		// 0) Make sure imagesInFlight matches swapchain image count (covers first init and any recreate)
		const auto& framebuffers = swapChainManager->GetFramebuffers();
		if (imagesInFlight.size() != framebuffers.size())
		{
			imagesInFlight.assign(framebuffers.size(), VK_NULL_HANDLE);
		}

		// 1) Wait for this frame’s per-frame fence (double/triple buffering)
		syncManager->WaitForFence(currentFrame);

		VkSemaphore imageAvailableSemaphore = syncManager->GetImageAvailableSemaphore(currentFrame);

		// 2) Acquire next image
		uint32_t imageIndex = 0;
		VkResult acquireResult = swapChainManager->AcquireNextImage(imageAvailableSemaphore, &imageIndex);

		// If OUT_OF_DATE (or we know a resize happened), we must still consume the signaled binary semaphore
		// to avoid reusing a signaled semaphore on the next frame.
		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || framebufferResized)
		{
			framebufferResized = false;

			// We will submit an empty batch that waits on imageAvailableSemaphore and signals the per-frame fence.
			// This "consumes" the binary semaphore, keeping synchronization valid.
			VkSubmitInfo emptySubmit{};
			emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
			// First usage of the acquired image would have been as color attachment; use that as the wait stage.
			// Using COLOR_ATTACHMENT_OUTPUT here is safe as a conservative choice for this empty submit.
			VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

			emptySubmit.waitSemaphoreCount = 1;
			emptySubmit.pWaitSemaphores = waitSemaphores;
			emptySubmit.pWaitDstStageMask = waitStages;

			// No command buffers.
			emptySubmit.commandBufferCount = 0;
			emptySubmit.pCommandBuffers = nullptr;

			// No signals needed for present; but we *do* need our fence to become unsignaled->signaled
			// so the next frame doesn't hard-stall. Reset and hand the fence to the empty submit.
			syncManager->ResetFence(currentFrame);
			VkFence inFlightFence = syncManager->GetInFlightFence(currentFrame);

			if (vkQueueSubmit(deviceManager->GetGraphicsQueue(), 1, &emptySubmit, inFlightFence) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to submit empty batch to consume acquire semaphore!");
			}

			// This swapchain image isn't presented; still track the fence associated with it to keep parity.
			if (imageIndex < imagesInFlight.size())
			{
				imagesInFlight[imageIndex] = inFlightFence;
			}

			// Defer actual swapchain recreation to Update()
			needsSwapchainRecreate = true;

			// Advance frame-in-flight index so we don't stall on the same fence next time
			currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
			return;
		}
		else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("Failed to acquire swap chain image!");
		}

		// For SUBOPTIMAL, we still render/present this frame but schedule a recreate.
		if (acquireResult == VK_SUBOPTIMAL_KHR)
		{
			needsSwapchainRecreate = true;
		}

		// 3) If this swapchain image is already in-flight, wait for its fence
		if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
		{
			vkWaitForFences(deviceManager->GetDevice(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
		}

		// 4) We will submit this frame; reset this frame’s fence now
		syncManager->ResetFence(currentFrame);

		// 5) Reset the command buffer for this image before re-recording
		const std::vector<VkCommandBuffer>& commandBuffers = commandManager->GetCommandBuffers();
		vkResetCommandBuffer(commandBuffers[imageIndex], 0);

		// 6) Record draw commands for this image
		RecordCommandBuffer(imageIndex);

		// 7) Submit
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		// First use of the swapchain image in this frame is as a color attachment.
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

		VkSemaphore renderFinishedSemaphore = syncManager->GetRenderFinishedSemaphore(currentFrame);
		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		VkFence inFlightFence = syncManager->GetInFlightFence(currentFrame);

		if (vkQueueSubmit(deviceManager->GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit draw command buffer!");
		}

		// 8) Mark this swapchain image as now being in flight with this fence
		imagesInFlight[imageIndex] = inFlightFence;

		// 9) Present
		VkResult presentResult = swapChainManager->Present(deviceManager->GetPresentQueue(), signalSemaphores, imageIndex);

		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
		{
			needsSwapchainRecreate = true;
		}
		else if (presentResult == VK_SUBOPTIMAL_KHR)
		{
			// We rendered successfully, but the chain isn't ideal. Recreate soon.
			needsSwapchainRecreate = true;
		}
		else if (presentResult != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to present swap chain image!");
		}

		// 10) Advance frame-in-flight index
		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	static bool hasUploadedOrtho = false;

	void VulkanRenderer::UpdateUniformBuffer()
	{
		cameraUBO.view = cameraSystem->GetViewMatrix();
		cameraUBO.proj = cameraSystem->GetProjectionMatrix();

		const Camera& camera = cameraSystem->GetCamera();

		// Calculate half FOV tangents - make sure signs are correct
		float tanHalfFovY = tan(glm::radians(camera.GetFOV() * 0.5f));
		float tanHalfFovX = tanHalfFovY * camera.GetAspect();

		cameraUBO.camParams.x = tanHalfFovX;
		cameraUBO.camParams.y = tanHalfFovY;
		cameraUBO.camParams.z = camera.GetNearClip();
		cameraUBO.camParams.w = camera.GetFarClip();

		// Since this projection is always the exact same values, we only have to do it once 
		if (!hasUploadedOrtho)
		{
			cameraUBO.screenView = glm::mat4(1.0f); // Identity

			cameraUBO.screenProj = glm::ortho(
				0.0f, VirtualCanvasWidth,
				VirtualCanvasHeight, 0.0f, // Flip Y for Vulkan
				-1.0f, 1.0f
			);

			hasUploadedOrtho = true;
		}

		cameraUBO.viewportSize = glm::vec2(windowWidth, windowHeight);

		descriptorManager->UpdatePerFrameUBO(currentFrame, cameraUBO);
	}

	void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex)
	{
		const std::vector<VkCommandBuffer>& commandBuffers = commandManager->GetCommandBuffers();
		VkCommandBuffer cmd = commandBuffers[imageIndex];

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // helpful for per-frame recording

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to begin recording command buffer!");
		}

		// Update camera UBO and instance buffer
		UpdateUniformBuffer();

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

		// Dynamic viewport & scissor
		VkViewport viewport{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
		VkRect2D scissor{ {0, 0}, extent };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// Skybox 
		if (cubemapController && cubemapController->IsEnabled())
		{
			if (auto* vkMap = static_cast<VulkanCubeMap*>(cubemapController->GetCubeMap()))
			{
				vkMap->Render(cmd, cameraSystem->GetViewMatrix(), cameraSystem->GetProjectionMatrix());
			}
		}

		// Scene pipeline & sets
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

		// This sets up fresh data for the frame and prepares every regular mesh to be draw in world space.
		indexDraw->UpdateInstanceBuffer(currentFrame); 

		// This then draws all of them with the default shader.
		indexDraw->DrawIndexedWorldMeshes(currentFrame, cmd); 

		// We now want to draw all of our text that is in the world.
		indexDraw->DrawIndexedMsdfText(currentFrame, cmd, TransformSpace::World);

		// This prepeares every screen space and UI decorated mesh, and draws all of them with the decorator shader.
		indexDraw->DrawIndexedScreenSpaceAndDecoratedMeshes(currentFrame, cmd); 

		// This prepares every screen space UI text and draws all of them with the msdf shader
		indexDraw->DrawIndexedMsdfText(currentFrame, cmd, TransformSpace::Screen);

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
		uint32_t mipLevels,                         
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
		imageInfo.mipLevels = mipLevels;            
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

	void VulkanRenderer::TransitionImageLayoutAllMipLevels(
		VkImage image,
		VkFormat format,
		uint32_t mipLevels,
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
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		commandManager->EndSingleTimeCommands(commandBuffer, deviceManager->GetGraphicsQueue());
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
		VkFormat format,
		uint32_t mipLevels                         
	)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels; // Support full mip chain
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView imageView;
		if (vkCreateImageView(deviceManager->GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create texture image view!");
		}

		return imageView;
	}

	void VulkanRenderer::GenerateMipmaps(
		VkImage image, 
		VkFormat imageFormat, 
		int32_t texWidth, 
		int32_t texHeight, 
		uint32_t mipLevels
	)
	{
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(deviceManager->GetPhysicalDevice(), imageFormat, &formatProperties);

		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			throw std::runtime_error("Texture format does not support linear blitting for mipmaps!");
		}

		VkCommandBuffer commandBuffer = commandManager->BeginSingleTimeCommands();

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < mipLevels; i++)
		{
			// Transition previous level to transfer source
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;

			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = {
				std::max(mipWidth / 2, 1),
				std::max(mipHeight / 2, 1),
				1
			};
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(
				commandBuffer,
				image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR
			);

			// Transition current mip level to shader read
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			mipWidth = std::max(mipWidth / 2, 1);
			mipHeight = std::max(mipHeight / 2, 1);
		}

		// Final barrier for the last mip level
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		auto graphicsQueue = deviceManager->GetGraphicsQueue();
		commandManager->EndSingleTimeCommands(commandBuffer, graphicsQueue);
	}

	// we should abstract this in the future if we need more than one sampler ever
	VkSampler VulkanRenderer::CreateSampler()
	{
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

		// Get device properties (for limits)
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(deviceManager->GetPhysicalDevice(), &properties);

		if (supportedFeatures.samplerAnisotropy)
		{
			samplerInfo.anisotropyEnable = VK_TRUE;

			// Clamp to the device's limit
			samplerInfo.maxAnisotropy = std::min(16.0f, properties.limits.maxSamplerAnisotropy);
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
		samplerInfo.mipLodBias = 0.0f; // might want to mess with this later..
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE; // Enables full mipmap usage

		VkSampler newSampler;
		if (vkCreateSampler(deviceManager->GetDevice(), &samplerInfo, nullptr, &newSampler) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create texture sampler!");
		}

		return newSampler;
	}

}
