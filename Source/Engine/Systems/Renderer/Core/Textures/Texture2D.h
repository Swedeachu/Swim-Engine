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
		Texture2D(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name = "<generated>");
		~Texture2D();

		void Free();
		void FreeCPU(); // stb image freeing

		uint32_t GetWidth() const { return width; }
		uint32_t GetHeight() const { return height; }
		const std::string& GetFilePath() const { return filePath; }

		VkImage GetImage() const { return image; }
		VkImageView GetImageView() const { return imageView; }

		uint32_t GetBindlessIndex() const { return bindlessIndex; }
		void SetBindlessIndex(uint32_t index) { bindlessIndex = index; }

		GLuint GetTextureID() const { return textureID; }

		unsigned char* GetData() const { return pixelData; }

		size_t GetDataSize() const { return width * height * 4; }

		bool isPixelDataSTB = true;

		static void FlushAllTextures(); // Frees everything still hanging around

	private:

		uint32_t width = 0;
		uint32_t height = 0;

		const std::string filePath;

		// Vulkan
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		uint32_t mipLevels = 1;

		uint32_t bindlessIndex = UINT32_MAX;

		GLuint textureID = 0;

		unsigned char* pixelData = nullptr;

		void LoadFromSTB();
		void Generate();
		void UploadToVulkan();
		void UploadToOpenGL();
		void GoBindless();

		bool freed = false;

		// Just a spot in memory where all textures are stored, solely for clean up on exit. Including procedural or GPU generated textures that never enter the client interfacing texture pool.
		static std::unordered_set<Texture2D*> allTextures;

	};

}
