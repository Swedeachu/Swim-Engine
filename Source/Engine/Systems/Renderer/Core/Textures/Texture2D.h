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

		// Vulkan-only
		void LoadVulkanTexture();
		void CreateImageView();
		void GoBindless();

		// OpenGL-only
		void LoadOpenGLTexture();

	};

}
