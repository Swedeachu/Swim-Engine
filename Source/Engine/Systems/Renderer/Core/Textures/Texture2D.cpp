#include "PCH.h"
#include "Texture2D.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Library/stb/stb_image.h"

namespace Engine
{

	static uint32_t vulkanTextureID = 0;

	std::unordered_set<Texture2D*> Texture2D::allTextures; // declare this for static init (defined in header)

	uint32_t GetMipLevels(float width, float height)
	{
		return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	}

	Texture2D::Texture2D(const std::string& filePath)
		: filePath(filePath)
	{
		LoadFromSTB();
		Generate();
	}

	// Last param name is optional
	Texture2D::Texture2D(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name)
		: width(width), height(height), filePath(name), isPixelDataSTB(false)
	{
		size_t dataSize = GetDataSize();

		if (dataSize == 0)
		{
			throw std::runtime_error("Texture2D(memory): data size is null!");
		}

		pixelData = static_cast<unsigned char*>(malloc(dataSize));

		if (!pixelData)
		{
			throw std::runtime_error("Texture2D(memory): pixel data malloc failed!");
		}

		memcpy(pixelData, rgbaData, dataSize);

		Generate();
	}

	void Texture2D::Generate()
	{
		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			UploadToVulkan();
			GoBindless();
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			UploadToOpenGL();
		}

		allTextures.insert(this);
	}

	Texture2D::~Texture2D()
	{
		Free();
		allTextures.erase(this);
	}

	void Texture2D::Free()
	{
		// First do this hack fix to avoid any accidental double frees.
		// This is stupid but just the easiest defensive fix.
		if (freed)
		{
			// std::cout << "Blocking double free on Texture: " << filePath << "\n";
			return;
		}
		freed = true;

		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			auto vulkanRenderer = SwimEngine::GetInstance()->GetVulkanRenderer();
			if (!vulkanRenderer) { return; }

			auto device = vulkanRenderer->GetDevice();
			if (!device) { return; }

			if (imageView) { vkDestroyImageView(device, imageView, nullptr); imageView = VK_NULL_HANDLE; }
			if (image) { vkDestroyImage(device, image, nullptr); image = VK_NULL_HANDLE; }
			if (memory) { vkFreeMemory(device, memory, nullptr); memory = VK_NULL_HANDLE; }
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			if (textureID != 0)
			{
				glDeleteTextures(1, &textureID);
				textureID = 0;
			}
		}

		FreeCPU();
	}

	void Texture2D::FreeCPU()
	{
		if (pixelData)
		{
			if (isPixelDataSTB)
			{
				stbi_image_free(pixelData);
			}
			else
			{
				free(pixelData);
			}
			pixelData = nullptr;
		}
	}

	void Texture2D::FlushAllTextures()
	{
		for (Texture2D* tex : allTextures)
		{
			if (tex)
			{
				tex->Free();
			}
		}
		allTextures.clear();
	}

	void Texture2D::LoadFromSTB()
	{
		int texWidth, texHeight, texChannels;
		pixelData = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (!pixelData)
		{
			throw std::runtime_error("Failed to load image: " + filePath);
		}
		width = static_cast<uint32_t>(texWidth);
		height = static_cast<uint32_t>(texHeight);
		isPixelDataSTB = true;
	}

	void Texture2D::UploadToVulkan()
	{
		auto vulkanRenderer = SwimEngine::GetInstance()->GetVulkanRenderer();
		if (!vulkanRenderer)
		{
			throw std::runtime_error("Texture2D::UploadToVulkan: VulkanRenderer not found!");
		}

		mipLevels = GetMipLevels(width, height);
		VkDeviceSize imageSize = GetDataSize();

		// Make the buffer for our texture memory
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		vulkanRenderer->CreateBuffer(
			imageSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingBufferMemory
		);

		// Map it in the device
		void* data = nullptr;
		vkMapMemory(vulkanRenderer->GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
		memcpy(data, pixelData, imageSize);
		vkUnmapMemory(vulkanRenderer->GetDevice(), stagingBufferMemory);

		// Create the image with proper flags for mip map support the sampler can use for selecting lods
		vulkanRenderer->CreateImage(
			width, height, mipLevels,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			image,
			memory
		);

		// Get it ready for shader
		vulkanRenderer->TransitionImageLayoutAllMipLevels(
			image,
			VK_FORMAT_R8G8B8A8_SRGB,
			mipLevels,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		);

		// Place the buffer in the image and then generate mip maps for the whole chain of lods
		vulkanRenderer->CopyBufferToImage(stagingBuffer, image, width, height);
		vulkanRenderer->GenerateMipmaps(
			image,
			VK_FORMAT_R8G8B8A8_SRGB,
			static_cast<int32_t>(width),
			static_cast<int32_t>(height),
			mipLevels
		);

		vkDestroyBuffer(vulkanRenderer->GetDevice(), stagingBuffer, nullptr);
		vkFreeMemory(vulkanRenderer->GetDevice(), stagingBufferMemory, nullptr);

		// Also one last thing to register the image view for the mip map chain
		imageView = vulkanRenderer->CreateImageView(
			image,
			VK_FORMAT_R8G8B8A8_SRGB,
			mipLevels
		);
	}

	void Texture2D::UploadToOpenGL()
	{
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_2D, textureID);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		GLfloat maxAniso = 0.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, maxAniso);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.0f);

		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGBA,
			width,
			height,
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			pixelData
		);

		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void Texture2D::GoBindless()
	{
		auto vulkanRenderer = SwimEngine::GetInstance()->GetVulkanRenderer();

		bindlessIndex = vulkanTextureID;
		const auto& descriptorManager = vulkanRenderer->GetDescriptorManager();

		if (descriptorManager && bindlessIndex != UINT32_MAX)
		{
			descriptorManager->UpdateBindlessTexture(bindlessIndex, imageView, vulkanRenderer->GetDefaultSampler());
		}

		vulkanTextureID++;
	}

}
