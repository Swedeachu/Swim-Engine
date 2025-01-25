#pragma once 

#include <vulkan/vulkan.hpp>

namespace Engine
{

  class Texture2D
  {

  public:

    Texture2D(const std::string& filePath);
    ~Texture2D();

    VkImage GetImage() const { return image; }
    VkImageView GetImageView() const { return imageView; }
    // VkSampler GetSampler() const { return sampler; }

  private:

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    // VkSampler sampler = VK_NULL_HANDLE;

    uint32_t width = 0;
    uint32_t height = 0;
    // ... maybe format, channels, etc.

    void LoadFromFile(const std::string& filePath);
    // void CreateImageResources();
    void CreateImageView();

  };

}