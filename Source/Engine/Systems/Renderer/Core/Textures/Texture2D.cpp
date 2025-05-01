#include "PCH.h"
#include "Texture2D.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "Library/stb/stb_image.h"

namespace Engine
{

  static uint32_t vulkanTextureID = 0;

  Texture2D::Texture2D(const std::string& filePath) : filePath(filePath)
  {
    if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
    {
      LoadVulkanTexture();
      CreateImageView();
      GoBindless();
    }
    else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
    {
      LoadOpenGLTexture();
    }
  }

  Texture2D::~Texture2D()
  {
    Free();
  }

  void Texture2D::Free()
  {
    if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
    {
      auto engine = SwimEngine::GetInstance();
      auto vulkanRenderer = engine.get()->GetVulkanRenderer();
      if (!vulkanRenderer) { return; }

      auto device = vulkanRenderer->GetDevice();
      if (!device) { return; }

      if (imageView)
      {
        vkDestroyImageView(device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
      }
      if (image)
      {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
      }
      if (memory)
      {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
      }
    }
    else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
    {
      if (textureID != 0)
      {
        glDeleteTextures(1, &textureID);
        textureID = 0;
      }
    }
  }

  void Texture2D::LoadVulkanTexture()
  {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels)
    {
      throw std::runtime_error("Failed to load image: " + filePath);
    }

    width = static_cast<uint32_t>(texWidth);
    height = static_cast<uint32_t>(texHeight);
    VkDeviceSize imageSize = width * height * 4;

    auto engine = SwimEngine::GetInstance();
    auto vulkanRenderer = engine.get()->GetVulkanRenderer();
    if (!vulkanRenderer)
    {
      stbi_image_free(pixels);
      throw std::runtime_error("Texture2D::LoadFromFile: VulkanRenderer not found!");
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    vulkanRenderer->CreateBuffer(
      imageSize,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      stagingBuffer,
      stagingBufferMemory
    );

    void* data;
    vkMapMemory(vulkanRenderer->GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanRenderer->GetDevice(), stagingBufferMemory);

    stbi_image_free(pixels);

    vulkanRenderer->CreateImage(
      width, height,
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      image,
      memory
    );

    vulkanRenderer->TransitionImageLayout(
      image,
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    vulkanRenderer->CopyBufferToImage(
      stagingBuffer,
      image,
      width,
      height
    );

    vulkanRenderer->TransitionImageLayout(
      image,
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    vkDestroyBuffer(vulkanRenderer->GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanRenderer->GetDevice(), stagingBufferMemory, nullptr);
  }

  void Texture2D::CreateImageView()
  {
    auto engine = SwimEngine::GetInstance();
    auto vulkanRenderer = engine.get()->GetVulkanRenderer();
    if (!vulkanRenderer)
    {
      throw std::runtime_error("Texture2D::CreateImageView: VulkanRenderer not found!");
    }

    imageView = vulkanRenderer->CreateImageView(
      image,
      VK_FORMAT_R8G8B8A8_SRGB
    );
  }

  void Texture2D::GoBindless()
  {
    auto engine = SwimEngine::GetInstance();
    auto vulkanRenderer = engine.get()->GetVulkanRenderer();

    // Register this texture in the bindless descriptor set
    bindlessIndex = vulkanTextureID;
    const std::unique_ptr<VulkanDescriptorManager>& descriptorManager = vulkanRenderer->GetDescriptorManager();
    if (descriptorManager && bindlessIndex != UINT32_MAX)
    {
      descriptorManager->UpdateBindlessTexture(bindlessIndex, imageView, vulkanRenderer->GetDefaultSampler());
    }

    // Increment count for the next one 
    vulkanTextureID++;

  #ifdef _DEBUG
    // Loading the texture is now fully finished here for Vulkan
    std::cout << "Loaded Texture2D (Vulkan): " << filePath << " (Bindless Index = " << bindlessIndex << ")" << std::endl;
  #endif
  }

  void Texture2D::LoadOpenGLTexture()
  {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels)
    {
      throw std::runtime_error("Failed to load image: " + filePath);
    }

    width = static_cast<uint32_t>(texWidth);
    height = static_cast<uint32_t>(texHeight);

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RGBA,
      width,
      height,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      pixels
    );

    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);

  #ifdef _DEBUG
    std::cout << "Loaded Texture2D (OpenGL): " << filePath << " -> ID " << textureID << std::endl;
  #endif
  }

}
