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

	bool operator==(const Texture2D& lhs, const Texture2D& rhs)
	{
		// Fast-path: if they are the same instance
		if (&lhs == &rhs)
		{
			return true;
		}

		// Compare dimensions
		if (lhs.GetWidth() != rhs.GetWidth() || lhs.GetHeight() != rhs.GetHeight())
		{
			return false;
		}

		// Compare pixel data size
		size_t size = lhs.GetDataSize();
		if (size != rhs.GetDataSize())
		{
			return false;
		}

		// Compare pixel data bytes
		const unsigned char* dataA = lhs.GetData();
		const unsigned char* dataB = rhs.GetData();

		if (dataA == nullptr || dataB == nullptr)
		{
			// Fail-safe: consider null data as unequal
			return false;
		}

		return std::memcmp(dataA, dataB, size) == 0;
	}

	Texture2D::Texture2D(const std::string& filePath, bool generateMips)
		: filePath(filePath), generateMips(generateMips)
	{
		LoadFromSTB();
		Generate();
	}

	// Last param name is optional
	Texture2D::Texture2D(uint32_t width, uint32_t height, const unsigned char* rgbaData, const std::string& name, bool generateMips)
		: width(width), height(height), filePath(name), isPixelDataSTB(false), generateMips(generateMips)
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
		if (!freed) allTextures.erase(this);
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

	int Texture2D::GetTextureCountOnGPU()
	{
		return allTextures.size();
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

		// Decide format & mip chain policy based on generateMips.
		// For MSDF/UI/data textures, we want linear UNORM + no mips.
		const bool useMips = generateMips;

		mipLevels = useMips ? GetMipLevels(width, height) : 1;

		const VkFormat format = useMips
			? VK_FORMAT_R8G8B8A8_SRGB   // color textures prefer sRGB + mips
			: VK_FORMAT_R8G8B8A8_UNORM; // MSDF/UI prefer linear (UNORM) + no mips

		const VkDeviceSize imageSize = GetDataSize();

		// --- 1) Staging buffer upload ----------------------------------------------------
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

		vulkanRenderer->CreateBuffer(
			imageSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingBufferMemory
		);

		{
			void* data = nullptr;
			vkMapMemory(vulkanRenderer->GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
			{
				// Copy the pixel payload into the staging buffer.
				std::memcpy(data, pixelData, static_cast<size_t>(imageSize));
			}
			vkUnmapMemory(vulkanRenderer->GetDevice(), stagingBufferMemory);
		}

		// --- 2) Image creation ------------------------------------------------------------
		// If we are going to generate mips, we must also be able to read from previous
		// levels (blit source) -> need TRANSFER_SRC usage.
		VkImageUsageFlags usage =
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		if (useMips)
		{
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}

		vulkanRenderer->CreateImage(
			width, height, mipLevels,
			format,
			VK_IMAGE_TILING_OPTIMAL,
			usage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			image,
			memory
		);

		// --- 3) Layout transition to receive the upload ----------------------------------
		vulkanRenderer->TransitionImageLayoutAllMipLevels(
			image,
			format,
			mipLevels,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		);

		// --- 4) Copy staging buffer -> base level of the image ----------------------------
		vulkanRenderer->CopyBufferToImage(stagingBuffer, image, width, height);

		// --- 5) Build mip chain OR finalize single-level layout ---------------------------
		if (useMips)
		{
			// Generates and uploads all levels and (commonly) leaves image in SHADER_READ_ONLY_OPTIMAL.
			vulkanRenderer->GenerateMipmaps(
				image,
				format,
				static_cast<int32_t>(width),
				static_cast<int32_t>(height),
				mipLevels
			);
		}
		else
		{
			// No mips: transition the single level directly to shader-read layout.
			vulkanRenderer->TransitionImageLayoutAllMipLevels(
				image,
				format,
				/*levelCount=*/1,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);
		}

		// --- 6) Cleanup staging resources -------------------------------------------------
		vkDestroyBuffer(vulkanRenderer->GetDevice(), stagingBuffer, nullptr);
		vkFreeMemory(vulkanRenderer->GetDevice(), stagingBufferMemory, nullptr);

		// --- 7) Image view for the full mip chain (or just level 0) ----------------------
		imageView = vulkanRenderer->CreateImageView(
			image,
			format,
			mipLevels
		);
	}

	void Texture2D::UploadToOpenGL()
	{
		// Create and bind the GL texture object.
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_2D, textureID);

		// Upload the base level. Use a sized, linear internal format for reliability.
		// If we need sRGB for color textures, we will need to handle that at creation time per asset type.
		const GLenum internalFormat = GL_RGBA8; // linear; avoids MSDF errors caused by sRGB
		const GLenum externalFormat = GL_RGBA;
		const GLenum externalType = GL_UNSIGNED_BYTE;

		glTexImage2D(
			GL_TEXTURE_2D,
			0,                  // level
			internalFormat,     // internal format (sized)
			width,
			height,
			0,                  // border = 0
			externalFormat,     // source format
			externalType,       // source type
			pixelData
		);

		if (generateMips)
		{
			// --- Mipped sampling branch (general color textures, normals, etc.) ---
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			// Anisotropy helps *only* with mips. Query and set the max available.
			GLfloat maxAniso = 0.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
			if (maxAniso < 1.0f)
			{
				maxAniso = 1.0f;
			}
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, maxAniso);

			// Optional LOD bias (keep 0 unless you’re doing special filtering tricks).
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.0f);

			// Build the mip chain.
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		else
		{
			// --- No-mips branch (MSDF, UI masks, data textures that must not blur/average) ---
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			// Constrain sampling to the base level (no mip chain).
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

			// Make sure anisotropy isn’t boosting a non-existent mip chain.
			// Setting to 1 disables it cleanly.
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 1.0f);
		}

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
