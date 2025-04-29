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

	// A struct to hold indices into queues we use
	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool isComplete()
		{
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	// Swapchain support details
	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities{ NULL };
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	class VulkanRenderer : public Machine
	{

	public:

		VulkanRenderer(HWND hwnd = nullptr, uint32_t width = 1920, uint32_t height = 1080)
			: windowHandle(hwnd), windowWidth(width), windowHeight(height), swapChainExtent({ 0, 0 }), swapChainImageFormat(VK_FORMAT_UNDEFINED)
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

		VkDevice& GetDevice() { return device; }
		VkPhysicalDevice& GetPhysicalDevice() { return physicalDevice; }

		VkDescriptorSet CreateDescriptorSet(const std::shared_ptr<Texture2D>& texture) const;

		// Needs to be called when the window changes size
		void SetSurfaceSize(uint32_t newWidth, uint32_t newHeight);

		// Will flag the vulkanRenderer to reload everything for the adjusted surface, called by the engine when finished resizing the window
		void SetFramebufferResized()
		{
			framebufferResized = true;
		}

		// Begin/End single-time commands (used for short-lived ops like layout transitions, copies, etc.)
		VkCommandBuffer BeginSingleTimeCommands() const;
		void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;

		// Creates a buffer and allocates memory for it
		void CreateBuffer(
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties,
			VkBuffer& buffer,
			VkDeviceMemory& bufferMemory
		) const;

		// Copies data from one buffer to another
		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

		// Find suitable memory type index
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

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

		// Vulkan core objects
		VkInstance instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue graphicsQueue = VK_NULL_HANDLE;
		VkQueue presentQueue = VK_NULL_HANDLE;

		VkSwapchainKHR swapChain = VK_NULL_HANDLE;
		std::vector<VkImage> swapChainImages;
		VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D swapChainExtent = { 0, 0 };
		std::vector<VkImageView> swapChainImageViews;
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> swapChainFramebuffers;

		// Depth resources for the swapchain
		std::vector<VkImage> depthImages;          // Holds depth images for each swapchain image
		std::vector<VkDeviceMemory> depthImageMemories; // Holds memory allocations for depth images
		std::vector<VkImageView> depthImageViews;  // Holds image views for the depth images

		VkCommandPool commandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> commandBuffers;

		// Synchronization
		static const int MAX_FRAMES_IN_FLIGHT = 2;
		size_t currentFrame = 0;

		std::vector<VkSemaphore> imageAvailableSemaphores;
		std::vector<VkSemaphore> renderFinishedSemaphores;
		std::vector<VkFence> inFlightFences;

		// Uniform buffer
		std::unique_ptr<VulkanBuffer> uniformBuffer;

		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

		bool framebufferResized = false;

		std::shared_ptr<CameraSystem> cameraSystem;

		std::vector<const char*> deviceExtensions = {
				VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		static std::vector<const char*> validationLayers;

		// Draw frame each update
		void DrawFrame();

		// Command buffer recording
		void RecordCommandBuffer(uint32_t imageIndex);

		// Methods
		void CreateInstance();
		void SetupDebugMessenger();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateSwapChain();
		void CreateImageViews();
		void CreateRenderPass();
		void CreateGraphicsPipeline();
		void CreateDepthResources();
		void CreateFramebuffers();
		void CreateCommandPool();
		void AllocateCommandBuffers();
		void CreateSyncObjects();
		void CreateDescriptorSetLayout();
		void CreateDescriptorSet();
		void CreatePipelineLayout();
		void CreateDescriptorPool();
		VkSampler CreateSampler();

		// Cleans up old swapchain objects
		void CleanupSwapChain();

		// Recreates the swapchain and dependent objects
		void RecreateSwapChain();

		inline MeshBufferData& GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh);

		void UpdateUniformBuffer();

		// Helpers
		int RateDeviceSuitability(VkPhysicalDevice device) const;
		bool CheckValidationLayerSupport() const;
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
		bool IsDeviceSuitable(VkPhysicalDevice device) const;
		std::vector<const char*> GetRequiredExtensions() const;

		VkFormat FindDepthFormat() const;
		VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;

		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

		void UpdateMaterialDescriptorSet(VkDescriptorSet dstSet, const Material& mat) const;

		// TODO: sampler and blend mode maps
		VkSampler defaultSampler = VK_NULL_HANDLE; // assigned from CreateSampler during Init
		// VkSampler activeSampler;

		std::shared_ptr<Texture2D> missingTexture;

	};

}
