// VulkanRenderer.h

#pragma once

#include <optional>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <vector>
#include "Buffer/VulkanBuffer.h"
#include "Engine/Components/Transform.h"
#include "Meshes/Vertex.h"
#include "Meshes/Mesh.h"

// Forward declare
struct GLFWwindow; // if we use GLFW in the future for windowing
// For now, we use Win32. We'll just rely on HWND from the engine.

namespace Engine
{

	struct CameraUBO
	{
		glm::mat4 view;
		glm::mat4 proj;
		// glm::mat4 model;
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
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
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

		// Call when window resized if needed:
		void OnWindowResize(uint32_t newWidth, uint32_t newHeight);

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
		VkFormat swapChainImageFormat;
		VkExtent2D swapChainExtent;
		std::vector<VkImageView> swapChainImageViews;
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> swapChainFramebuffers;

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
		void CreateFramebuffers();
		void CreateCommandPool();
		void AllocateCommandBuffers();
		void CreateSyncObjects();
		void CreateDescriptorSetLayout();
		void CreateDescriptorSet();
		void CreatePipelineLayout();

		MeshBufferData& GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh);

		void UpdateUniformBuffer();

		// Helpers
		bool CheckValidationLayerSupport();
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
		bool IsDeviceSuitable(VkPhysicalDevice device);
		std::vector<const char*> GetRequiredExtensions();

		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code);

	};

}
