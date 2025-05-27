#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <glad/gl.h>

namespace Engine
{

	class Texture2D
	{

	public:

		// Loads from disk
		Texture2D(const std::string& filePath);

		// Constructs a Texture2D from raw RGBA memory data 
		Texture2D(uint32_t width, uint32_t height, const unsigned char* rgbaData);

		~Texture2D();

		void Free();

		// Getters from original file from disk (do not confuse file width and height with transform scales)
		uint32_t GetWidth() const { return width; }
		uint32_t GetHeight() const { return height; }
		const std::string& GetFilePath()  const { return filePath; }

		// Vulkan accessors
		VkImage GetImage() const { return image; }
		VkImageView GetImageView() const { return imageView; }

		// Bindless texture index
		uint32_t GetBindlessIndex() const { return bindlessIndex; }
		void SetBindlessIndex(uint32_t index) { bindlessIndex = index; }

		// OpenGL accessor
		GLuint GetTextureID() const { return textureID; }

		// Get raw pixel data
		unsigned char* GetData() const { return pixelData; }

		size_t GetDataSize() const
		{
			return width * height * 4; // not sure how correct this is
		}

	private:

		uint32_t width = 0;
		uint32_t height = 0;

		const std::string filePath;

		// Vulkan
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;

		// Bindless
		uint32_t bindlessIndex = UINT32_MAX; // Invalid/default until assigned

		// OpenGL
		GLuint textureID = 0;

		// Raw CPU-side pixel data 
		unsigned char* pixelData = nullptr;

		bool isPixelDataSTB = true; // Determines if pixelData was loaded via stbi (true) or malloc (false)

		// Vulkan-only
		void LoadVulkanTexture();
		void CreateImageView();
		void GoBindless();

		// OpenGL-only
		void LoadOpenGLTexture();

	};

}
