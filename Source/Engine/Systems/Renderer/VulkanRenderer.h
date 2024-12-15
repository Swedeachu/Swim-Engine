#pragma once

#include <optional>
#include <stdexcept>
#include <functional>
#include <fstream>
#include "Buffer/VulkanBuffer.h"

#include "Engine/Components/Transform.h"
#include "Engine/Components/Mesh.h"

// Forward declare
struct GLFWwindow; // if you use GLFW in the future for windowing
// For now, we use Win32. We'll just rely on HWND from the engine.

namespace Engine
{

  struct CameraUBO
  {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 model;
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

    // the only point of these default params is to provide a default ctor
    VulkanRenderer(HWND hwnd = nullptr, uint32_t width = 1920, uint32_t height = 1080)
      : windowHandle(hwnd), windowWidth(width), windowHeight(height)
    {
      // Ensure hwnd is valid
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

    // Draw frame each update
    void DrawFrame();
    void SubmitMesh(const Transform& transform, const Mesh& mesh);
    void RecordCommandBuffers();

    // Call when window resized if needed:
    void OnWindowResize(uint32_t newWidth, uint32_t newHeight);

    // void UpdateUniformBuffer(const CameraUBO& uboData);

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

    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    std::unique_ptr<VulkanBuffer> uniformBuffer;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorPool descriptorPool;

    bool framebufferResized = false;

    // Internal setup methods
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
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateBuffers(const Mesh& mesh);
    void CreateDescriptorSetLayout();
    void CreateDescriptorSet();

    struct Renderable
    {
      Transform transform;
      Mesh mesh;
    };

    std::vector<Renderable> renderables; // Store entities with meshes
    std::vector<VkDescriptorSet> descriptorSets; // Store descriptor sets for each renderable

    void UpdateUniformBuffer(const Transform& transform);

    // Helpers
    bool CheckValidationLayerSupport();
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    bool IsDeviceSuitable(VkPhysicalDevice device);

    static std::vector<const char*> GetRequiredExtensions();
    static std::vector<const char*> validationLayers;

    std::shared_ptr<CameraSystem> cameraSystem;

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Shader loading helper
    static std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);

  };

}
