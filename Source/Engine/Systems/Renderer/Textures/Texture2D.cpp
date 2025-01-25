#include "PCH.h"
#include "Texture2D.h"
#include "Engine/Systems/Renderer/VulkanRenderer.h"
#include "Engine/SwimEngine.h"

#define STB_IMAGE_IMPLEMENTATION
#include "Library/stb/stb_image.h"

namespace Engine
{

  Texture2D::Texture2D(const std::string& filePath)
  {
    LoadFromFile(filePath);
    CreateImageView();
  }

  Texture2D::~Texture2D()
  {
    // Clean up
    // We'll get the VulkanRenderer to do it since it has the device handle
    auto engine = SwimEngine::GetInstance();
    auto renderer = engine.get()->GetRenderer();
    if (!renderer) { return; }

    if (imageView)
    {
      vkDestroyImageView(renderer->GetDevice(), imageView, nullptr);
      imageView = VK_NULL_HANDLE;
    }
    if (image)
    {
      vkDestroyImage(renderer->GetDevice(), image, nullptr);
      image = VK_NULL_HANDLE;
    }
    if (memory)
    {
      vkFreeMemory(renderer->GetDevice(), memory, nullptr);
      memory = VK_NULL_HANDLE;
    }
  }

  void Texture2D::LoadFromFile(const std::string& filePath)
  {
    // 1) Load pixels with stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels)
    {
      throw std::runtime_error("Failed to load image: " + filePath);
    }

    auto engine = SwimEngine::GetInstance();
    auto renderer = engine.get()->GetRenderer();

    width = static_cast<uint32_t>(texWidth);
    height = static_cast<uint32_t>(texHeight);

    // 2) Create staging buffer
    VkDeviceSize imageSize = width * height * 4; // RGBA
    if (!renderer)
    {
      stbi_image_free(pixels);
      throw std::runtime_error("Texture2D::LoadFromFile: VulkanRenderer not found!");
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->CreateBuffer(
      imageSize,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      stagingBuffer,
      stagingBufferMemory
    );

    // 3) Map staging buffer and copy
    void* data;
    vkMapMemory(renderer->GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(renderer->GetDevice(), stagingBufferMemory);

    stbi_image_free(pixels); // done with CPU data

    // 4) Create the final Vulkan image
    renderer->CreateImage(
      width, height,
      VK_FORMAT_R8G8B8A8_SRGB, // or VK_FORMAT_R8G8B8A8_UNORM if you prefer
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      image,
      memory
    );

    // 5) Transition the image layout to be ready for copy
    renderer->TransitionImageLayout(
      image,
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    // 6) Copy buffer -> image
    renderer->CopyBufferToImage(
      stagingBuffer,
      image,
      width,
      height
    );

    // 7) Transition image for shader usage
    renderer->TransitionImageLayout(
      image,
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // 8) Cleanup staging buffer
    vkDestroyBuffer(renderer->GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->GetDevice(), stagingBufferMemory, nullptr);

  #ifdef _DEBUG
    std::cout << "Loaded Texture2D: " << filePath << std::endl;
  #endif
  }

  void Texture2D::CreateImageView()
  {
    auto engine = SwimEngine::GetInstance();
    auto renderer = engine.get()->GetRenderer();
    if (!renderer)
    {
      throw std::runtime_error("Texture2D::CreateImageView: VulkanRenderer not found!");
    }

    imageView = renderer->CreateImageView(
      image,
      VK_FORMAT_R8G8B8A8_SRGB
    );
  }

}
