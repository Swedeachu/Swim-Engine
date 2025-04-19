#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <glad/gl.h>

namespace Engine
{

  class Texture2D
  {

  public:

    Texture2D(const std::string& filePath);
    ~Texture2D();

    void Free();

    // Vulkan accessors
    VkImage GetImage() const { return image; }
    VkImageView GetImageView() const { return imageView; }

    // OpenGL accessor
    GLuint GetTextureID() const { return textureID; }

  private:

    uint32_t width = 0;
    uint32_t height = 0;

    // Vulkan
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;

    // OpenGL
    GLuint textureID = 0;

    void LoadVulkanTexture(const std::string& filePath);
    void CreateImageView(); // Vulkan-only
    void LoadOpenGLTexture(const std::string& filePath); // OpenGL-only

  };

}
