#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Engine/Components/Material.h"
#include "VulkanDeviceManager.h"
#include "VulkanSwapChain.h"
#include "VulkanCommandManager.h"
#include "VulkanSyncManager.h"
#include "VulkanPipelineManager.h"
#include "VulkanDescriptorManager.h"
#include "VulkanIndexDraw.h"
#include "Buffers/VulkanBuffer.h"
#include "Buffers/VulkanInstanceBuffer.h"
#include "Engine/Systems/Renderer/Renderer.h"
#include "Engine/Systems/Renderer/Core/Environment/CubeMapController.h"

namespace Engine
{

	class VulkanRenderer : public Renderer
	{

	public:

		void Create(Swim::Platform::Window& window, uint32_t width, uint32_t height) override;

		// Machine overrides
		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		std::unique_ptr<CubeMapController>& GetCubeMapController() override { return cubemapController; }

		void UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData) override;

		// this should shortcut from VulkanDeviceManager
		VkDevice GetDevice() const { return deviceManager->GetDevice(); }
		VkPhysicalDevice GetPhysicalDevice() const { return deviceManager->GetPhysicalDevice(); }

		const std::unique_ptr<VulkanDeviceManager>& GetDeviceManager() { return deviceManager; }
		const std::unique_ptr<VulkanDescriptorManager>& GetDescriptorManager() const { return descriptorManager; }
		const VkSampler& GetDefaultSampler() const { return defaultSampler; }
		const std::unique_ptr<VulkanIndexDraw>& GetIndexDraw() const { return indexDraw; }
		const std::unique_ptr<VulkanCommandManager>& GetCommandManager() const { return commandManager; }
		const std::unique_ptr<VulkanPipelineManager>& GetPipelineManager() const { return pipelineManager; }

		const uint32_t GetCurrentFrameIndex() const { return currentFrame; }

		// Needs to be called when the window changes size
		void SetSurfaceSize(uint32_t newWidth, uint32_t newHeight);
		uint32_t GetSurfaceWidth() const { return windowWidth; }
		uint32_t GetSurfaceHeight() const { return windowHeight; }
		void SetCameraSystem(CameraSystem* system) { cameraSystem = system; }
		void SetRenderScene(Scene* scene) override
		{
			Renderer::SetRenderScene(scene);
			if (indexDraw)
			{
				indexDraw->SetScene(scene);
			}
		}

		// For MSAA
		const VkSampleCountFlagBits GetSampleCountFlagBits() const { return msaaSamples; }

		// Will flag the vulkanRenderer to reload everything for the adjusted surface, called by the engine when finished resizing the window
		void SetFramebufferResized()
		{
			framebufferResized = true;
		}

		const CameraUBO& GetCameraUBO() const { return cameraUBO; }

		// Creates a buffer and allocates memory for it
		void CreateBuffer(
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties,
			VkBuffer& buffer,
			VkDeviceMemory& bufferMemory
		) const;

		// Copies data from one buffer to another
		void CopyBuffer(
			VkBuffer srcBuffer,
			VkBuffer dstBuffer,
			VkDeviceSize size
		) const;

		// Copies data from one buffer to another with an offset
		void CopyBuffer(
			VkBuffer srcBuffer,
			VkBuffer dstBuffer,
			VkDeviceSize size,
			VkDeviceSize dstOffset
		) const;

		// Creates a 2D image on the GPU
		void CreateImage(
			uint32_t width,
			uint32_t height,
			uint32_t mipLevels,
			VkFormat format,
			VkImageTiling tiling,
			VkImageUsageFlags usage,
			VkMemoryPropertyFlags properties,
			VkImage& outImage,
			VkDeviceMemory& outImageMemory
		);

		// Transition image layout + mips (e.g. Undefined -> TransferDst -> ShaderReadOnly)
		void TransitionImageLayoutAllMipLevels(
			VkImage image,
			VkFormat format,
			uint32_t mipLevels,
			VkImageLayout oldLayout,
			VkImageLayout newLayout
		);

		// Copy from a buffer into an image
		void CopyBufferToImage(
			VkBuffer buffer,
			VkImage image,
			uint32_t width,
			uint32_t height
		);

		// Create a standard 2D image view
		VkImageView CreateImageView(
			VkImage image,
			VkFormat format,
			uint32_t mipLevels
		);

		// Uses vkCmdBlitImage() to make the chain of mip maps for a texture
		void GenerateMipmaps(
			VkImage image,
			VkFormat imageFormat,
			int32_t texWidth,
			int32_t texHeight,
			uint32_t mipLevels
		);

	private:

		// Window management
		Swim::Platform::Window* window = nullptr;
		uint32_t windowWidth;
		uint32_t windowHeight;

		CameraUBO cameraUBO{};

		std::unique_ptr<VulkanDeviceManager> deviceManager;
		std::unique_ptr<VulkanSwapChain> swapChainManager;
		std::unique_ptr<VulkanPipelineManager> pipelineManager;
		std::unique_ptr<VulkanCommandManager> commandManager;

		// Synchronization values for SyncManager and DescriptorManager to use for buffering.
		// Maybe MAX_FRAMES_IN_FLIGHT should be an engine wide constant? 
		// Triple buffering improves overlap for the GPU-driven path without changing the higher-level frame model.
		static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
		uint32_t currentFrame = 0;
		bool needsSwapchainRecreate = false;

		// Ideally x4, set from VulkanDeviceManager::GetMaxUsableSampleCount()
		VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

		std::unique_ptr<VulkanSyncManager> syncManager;
		std::unique_ptr<VulkanDescriptorManager> descriptorManager;
		std::unique_ptr<VulkanIndexDraw> indexDraw;

		std::vector<VkFence> imagesInFlight;

		bool framebufferResized = false;

		CameraSystem* cameraSystem = nullptr;

		void DrawFrame();

		void RecordCommandBuffer(uint32_t imageIndex);

		VkSampler CreateSampler();

		void UpdateUniformBuffer();

		// TODO: sampler and blend mode maps
		VkSampler defaultSampler = VK_NULL_HANDLE; // assigned from CreateSampler during Init
		// VkSampler activeSampler;

		std::shared_ptr<Texture2D> missingTexture;

		std::unique_ptr<CubeMapController> cubemapController;

	};

}
