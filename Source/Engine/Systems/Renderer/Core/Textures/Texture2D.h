#pragma once

#include "Engine/EngineConfig.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>
#include <glad/gl.h>

namespace Engine
{

	class VulkanRenderer;
	class TexturePool;

	struct TextureLifetimeTracker
	{
		std::atomic<int> LiveCount{ 0 };
		std::atomic<uint32_t> NextBindlessIndex{ 0 };
	};

	struct TextureRuntimeContext
	{
		GraphicsBackend Backend = GraphicsBackend::Vulkan;
		VulkanRenderer* Vulkan = nullptr;
		std::shared_ptr<TextureLifetimeTracker> Lifetime;
	};

	class Texture2D
	{

	public:

		Texture2D(const std::string& filePath, bool generateMips = true);
		Texture2D(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name = "<generated>", bool generateMips = true);
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

		size_t GetDataSize() const { return static_cast<size_t>(width) * static_cast<size_t>(height) * 4u; }
		bool IsResident() const { return resident; }

		bool isPixelDataSTB = true;
		bool generateMips = true; 


	private:
		friend class TexturePool;

		uint32_t width = 0;
		uint32_t height = 0;

		TextureRuntimeContext context{};

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
		void MakeResident(TextureRuntimeContext residencyContext);
		void UploadToVulkan();
		void UploadToOpenGL();
		void GoBindless();

		bool resident = false;
		bool lifetimeCounted = false;
		bool freed = false;

	};

	bool operator==(const Texture2D& lhs, const Texture2D& rhs);

}
