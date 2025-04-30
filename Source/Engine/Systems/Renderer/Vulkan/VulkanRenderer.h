#pragma once

#include <optional>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <vector>
#include "VulkanBuffer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Systems/Renderer/Core/Meshes/Vertex.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "VulkanDeviceManager.h"
#include "VulkanSwapChain.h"
#include "VulkanCommandManager.h"
#include "VulkanSyncManager.h"
#include "VulkanPipelineManager.h"
#include "VulkanDescriptorManager.h"

// Forward declare
struct GLFWwindow; // if we use GLFW in the future for windowing
// For now, we use Win32. We'll just rely on HWND from the engine.

namespace Engine
{

	struct PushConstantData
	{
		glm::mat4 model;     // The model transform
		float hasTexture;    // 1.0 = use albedoMap, 0.0 = use vertex color
		// We pad to 16 bytes; push constants must align to 4-byte boundaries.
		float padA;
		float padB;
		float padC;
	};

	class VulkanRenderer : public Machine
	{

	public:

		VulkanRenderer(HWND hwnd = nullptr, uint32_t width = 1920, uint32_t height = 1080)
			: windowHandle(hwnd), windowWidth(width), windowHeight(height)
		{
			if (!windowHandle)
			{
				throw std::runtime_error("Invalid window handle passed to VulkanRenderer.");
			}
		}

		// Machine overrides
		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		// this should shortcut from VulkanDeviceManager
		const VkDevice& GetDevice() const { return deviceManager->GetDevice(); }
		const VkPhysicalDevice& GetPhysicalDevice() const { return deviceManager->GetPhysicalDevice(); }

		VkDescriptorSet CreateDescriptorSet(const std::shared_ptr<Texture2D>& texture) const;

		// Needs to be called when the window changes size
		void SetSurfaceSize(uint32_t newWidth, uint32_t newHeight);

		// Will flag the vulkanRenderer to reload everything for the adjusted surface, called by the engine when finished resizing the window
		void SetFramebufferResized()
		{
			framebufferResized = true;
		}

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

		// Creates a 2D image on the GPU
		void CreateImage(
			uint32_t width,
			uint32_t height,
			VkFormat format,
			VkImageTiling tiling,
			VkImageUsageFlags usage,
			VkMemoryPropertyFlags properties,
			VkImage& outImage,
			VkDeviceMemory& outImageMemory
		);

		// Transition image layout (e.g. Undefined -> TransferDst -> ShaderReadOnly)
		void TransitionImageLayout(
			VkImage image,
			VkFormat format,
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
			VkFormat format
		);

	private:

		// Window management
		HWND windowHandle;
		uint32_t windowWidth;
		uint32_t windowHeight;

		std::unique_ptr<VulkanDeviceManager> deviceManager;
		std::unique_ptr<VulkanSwapChain> swapChainManager;
		std::unique_ptr<VulkanPipelineManager> pipelineManager;
		std::unique_ptr<VulkanCommandManager> commandManager;

		// Synchronization values for SyncManager to use
		static constexpr int MAX_FRAMES_IN_FLIGHT = 2; 
		size_t currentFrame = 0; 

		std::unique_ptr<VulkanSyncManager> syncManager;

		std::unique_ptr<VulkanBuffer> uniformBuffer;

		std::unique_ptr<VulkanDescriptorManager> descriptorManager;

		bool framebufferResized = false;

		std::shared_ptr<CameraSystem> cameraSystem;

		void DrawFrame();

		void RecordCommandBuffer(uint32_t imageIndex);

		VkSampler CreateSampler();

		inline MeshBufferData& GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh);

		void UpdateUniformBuffer();

		void UpdateMaterialDescriptorSet(VkDescriptorSet dstSet, const Material& mat) const;

		// TODO: sampler and blend mode maps
		VkSampler defaultSampler = VK_NULL_HANDLE; // assigned from CreateSampler during Init
		// VkSampler activeSampler;

		std::shared_ptr<Texture2D> missingTexture;

	};

}
