#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

namespace Swim::RhiVulkan
{

		std::uint32_t GetMaximumSampleCount(VkSampleCountFlags counts)
		{
			if ((counts & VK_SAMPLE_COUNT_64_BIT) != 0)
			{
				return 64;
			}
			if ((counts & VK_SAMPLE_COUNT_32_BIT) != 0)
			{
				return 32;
			}
			if ((counts & VK_SAMPLE_COUNT_16_BIT) != 0)
			{
				return 16;
			}
			if ((counts & VK_SAMPLE_COUNT_8_BIT) != 0)
			{
				return 8;
			}
			if ((counts & VK_SAMPLE_COUNT_4_BIT) != 0)
			{
				return 4;
			}
			if ((counts & VK_SAMPLE_COUNT_2_BIT) != 0)
			{
				return 2;
			}
			return 1;
		}

		bool HasExtension(const std::vector<std::string>& extensions, std::string_view name)
		{
			return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
		}

		VkFormat ToVkFormat(Rhi::Format format)
		{
			switch (format)
			{
			case Rhi::Format::R8Unorm: return VK_FORMAT_R8_UNORM;
			case Rhi::Format::R8Snorm: return VK_FORMAT_R8_SNORM;
			case Rhi::Format::R8Uint: return VK_FORMAT_R8_UINT;
			case Rhi::Format::R8Sint: return VK_FORMAT_R8_SINT;
			case Rhi::Format::R16Unorm: return VK_FORMAT_R16_UNORM;
			case Rhi::Format::R16Snorm: return VK_FORMAT_R16_SNORM;
			case Rhi::Format::R16Uint: return VK_FORMAT_R16_UINT;
			case Rhi::Format::R16Sint: return VK_FORMAT_R16_SINT;
			case Rhi::Format::R16Float: return VK_FORMAT_R16_SFLOAT;
			case Rhi::Format::R32Uint: return VK_FORMAT_R32_UINT;
			case Rhi::Format::R32Sint: return VK_FORMAT_R32_SINT;
			case Rhi::Format::R32Float: return VK_FORMAT_R32_SFLOAT;

			case Rhi::Format::RG8Unorm: return VK_FORMAT_R8G8_UNORM;
			case Rhi::Format::RG8Snorm: return VK_FORMAT_R8G8_SNORM;
			case Rhi::Format::RG8Uint: return VK_FORMAT_R8G8_UINT;
			case Rhi::Format::RG8Sint: return VK_FORMAT_R8G8_SINT;
			case Rhi::Format::RG16Unorm: return VK_FORMAT_R16G16_UNORM;
			case Rhi::Format::RG16Snorm: return VK_FORMAT_R16G16_SNORM;
			case Rhi::Format::RG16Uint: return VK_FORMAT_R16G16_UINT;
			case Rhi::Format::RG16Sint: return VK_FORMAT_R16G16_SINT;
			case Rhi::Format::RG16Float: return VK_FORMAT_R16G16_SFLOAT;
			case Rhi::Format::RG32Uint: return VK_FORMAT_R32G32_UINT;
			case Rhi::Format::RG32Sint: return VK_FORMAT_R32G32_SINT;
			case Rhi::Format::RG32Float: return VK_FORMAT_R32G32_SFLOAT;

			case Rhi::Format::RGB32Uint: return VK_FORMAT_R32G32B32_UINT;
			case Rhi::Format::RGB32Sint: return VK_FORMAT_R32G32B32_SINT;
			case Rhi::Format::RGB32Float: return VK_FORMAT_R32G32B32_SFLOAT;

			case Rhi::Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
			case Rhi::Format::RGBA8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
			case Rhi::Format::RGBA8Snorm: return VK_FORMAT_R8G8B8A8_SNORM;
			case Rhi::Format::RGBA8Uint: return VK_FORMAT_R8G8B8A8_UINT;
			case Rhi::Format::RGBA8Sint: return VK_FORMAT_R8G8B8A8_SINT;
			case Rhi::Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
			case Rhi::Format::BGRA8UnormSrgb: return VK_FORMAT_B8G8R8A8_SRGB;
			case Rhi::Format::RGBA16Unorm: return VK_FORMAT_R16G16B16A16_UNORM;
			case Rhi::Format::RGBA16Snorm: return VK_FORMAT_R16G16B16A16_SNORM;
			case Rhi::Format::RGBA16Uint: return VK_FORMAT_R16G16B16A16_UINT;
			case Rhi::Format::RGBA16Sint: return VK_FORMAT_R16G16B16A16_SINT;
			case Rhi::Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
			case Rhi::Format::RGBA32Uint: return VK_FORMAT_R32G32B32A32_UINT;
			case Rhi::Format::RGBA32Sint: return VK_FORMAT_R32G32B32A32_SINT;
			case Rhi::Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;

			case Rhi::Format::RGB10A2Unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			case Rhi::Format::RGB10A2Uint: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			case Rhi::Format::R11G11B10Float: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			case Rhi::Format::RGB9E5Float: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;

			case Rhi::Format::D16Unorm: return VK_FORMAT_D16_UNORM;
			case Rhi::Format::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
			case Rhi::Format::D32Float: return VK_FORMAT_D32_SFLOAT;
			case Rhi::Format::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;

			case Rhi::Format::BC1RGBAUnorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			case Rhi::Format::BC1RGBAUnormSrgb: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
			case Rhi::Format::BC3Unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
			case Rhi::Format::BC3UnormSrgb: return VK_FORMAT_BC3_SRGB_BLOCK;
			case Rhi::Format::BC4Unorm: return VK_FORMAT_BC4_UNORM_BLOCK;
			case Rhi::Format::BC4Snorm: return VK_FORMAT_BC4_SNORM_BLOCK;
			case Rhi::Format::BC5Unorm: return VK_FORMAT_BC5_UNORM_BLOCK;
			case Rhi::Format::BC5Snorm: return VK_FORMAT_BC5_SNORM_BLOCK;
			case Rhi::Format::BC6HUfloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			case Rhi::Format::BC6HSfloat: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			case Rhi::Format::BC7Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
			case Rhi::Format::BC7UnormSrgb: return VK_FORMAT_BC7_SRGB_BLOCK;

			case Rhi::Format::ETC2RGB8Unorm: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
			case Rhi::Format::ETC2RGB8UnormSrgb: return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
			case Rhi::Format::ETC2RGBA8Unorm: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
			case Rhi::Format::ETC2RGBA8UnormSrgb: return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;

			case Rhi::Format::ASTC4x4Unorm: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
			case Rhi::Format::ASTC4x4UnormSrgb: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
			case Rhi::Format::ASTC6x6Unorm: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
			case Rhi::Format::ASTC6x6UnormSrgb: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
			case Rhi::Format::ASTC8x8Unorm: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
			case Rhi::Format::ASTC8x8UnormSrgb: return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
			default: return VK_FORMAT_UNDEFINED;
			}
		}

		Rhi::Format FromVkFormat(VkFormat format)
		{
			switch (format)
			{
			case VK_FORMAT_R8G8B8A8_UNORM: return Rhi::Format::RGBA8Unorm;
			case VK_FORMAT_R8G8B8A8_SRGB: return Rhi::Format::RGBA8UnormSrgb;
			case VK_FORMAT_B8G8R8A8_UNORM: return Rhi::Format::BGRA8Unorm;
			case VK_FORMAT_B8G8R8A8_SRGB: return Rhi::Format::BGRA8UnormSrgb;
			case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return Rhi::Format::RGB10A2Unorm;
			default: return Rhi::Format::Undefined;
			}
		}

		VkSampleCountFlagBits ToVkSampleCount(Rhi::SampleCount samples)
		{
			switch (samples)
			{
			case Rhi::SampleCount::X1: return VK_SAMPLE_COUNT_1_BIT;
			case Rhi::SampleCount::X2: return VK_SAMPLE_COUNT_2_BIT;
			case Rhi::SampleCount::X4: return VK_SAMPLE_COUNT_4_BIT;
			case Rhi::SampleCount::X8: return VK_SAMPLE_COUNT_8_BIT;
			default: return VK_SAMPLE_COUNT_1_BIT;
			}
		}

		bool HasBufferUsage(Rhi::BufferUsage value, Rhi::BufferUsage flag)
		{
			return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
		}

		bool HasTextureUsage(Rhi::TextureUsage value, Rhi::TextureUsage flag)
		{
			return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
		}

		VkBufferUsageFlags ToVkBufferUsage(Rhi::BufferUsage usage)
		{
			VkBufferUsageFlags result = 0;
			if (HasBufferUsage(usage, Rhi::BufferUsage::TransferSource))
			{
				result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::TransferDestination))
			{
				result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::Vertex))
			{
				result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::Index))
			{
				result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::Uniform))
			{
				result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::Storage))
			{
				result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::Indirect))
			{
				result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::ShaderDeviceAddress))
			{
				result |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::AccelerationStructureStorage))
			{
				result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
			}
			if (HasBufferUsage(usage, Rhi::BufferUsage::AccelerationStructureBuildInput))
			{
				result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			}
			return result;
		}

		VkImageUsageFlags ToVkImageUsage(Rhi::TextureUsage usage)
		{
			VkImageUsageFlags result = 0;
			if (HasTextureUsage(usage, Rhi::TextureUsage::TransferSource))
			{
				result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			}
			if (HasTextureUsage(usage, Rhi::TextureUsage::TransferDestination))
			{
				result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			if (HasTextureUsage(usage, Rhi::TextureUsage::Sampled))
			{
				result |= VK_IMAGE_USAGE_SAMPLED_BIT;
			}
			if (HasTextureUsage(usage, Rhi::TextureUsage::Storage))
			{
				result |= VK_IMAGE_USAGE_STORAGE_BIT;
			}
			if (HasTextureUsage(usage, Rhi::TextureUsage::ColorAttachment))
			{
				result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			}
			if (HasTextureUsage(usage, Rhi::TextureUsage::DepthStencilAttachment))
			{
				result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			}
			if (HasTextureUsage(usage, Rhi::TextureUsage::TransientAttachment))
			{
				result |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
			}
			return result;
		}

		VkImageAspectFlags GetImageAspectMask(Rhi::Format format)
		{
			if (!Rhi::IsDepthFormat(format))
			{
				return VK_IMAGE_ASPECT_COLOR_BIT;
			}

			VkImageAspectFlags result = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (Rhi::HasStencil(format))
			{
				result |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
			return result;
		}

		VkImageType ToVkImageType(Rhi::TextureDimension dimension)
		{
			switch (dimension)
			{
			case Rhi::TextureDimension::Texture1D: return VK_IMAGE_TYPE_1D;
			case Rhi::TextureDimension::Texture2D:
			case Rhi::TextureDimension::TextureCube: return VK_IMAGE_TYPE_2D;
			case Rhi::TextureDimension::Texture3D: return VK_IMAGE_TYPE_3D;
			default: return VK_IMAGE_TYPE_2D;
			}
		}

		VkImageViewType ToVkImageViewType(Rhi::TextureViewDimension dimension)
		{
			switch (dimension)
			{
			case Rhi::TextureViewDimension::Texture1D: return VK_IMAGE_VIEW_TYPE_1D;
			case Rhi::TextureViewDimension::Texture1DArray: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case Rhi::TextureViewDimension::Texture2D: return VK_IMAGE_VIEW_TYPE_2D;
			case Rhi::TextureViewDimension::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case Rhi::TextureViewDimension::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
			case Rhi::TextureViewDimension::TextureCube: return VK_IMAGE_VIEW_TYPE_CUBE;
			case Rhi::TextureViewDimension::TextureCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			default: return VK_IMAGE_VIEW_TYPE_2D;
			}
		}

		bool ValidateTextureDesc(const Rhi::TextureDesc& desc)
		{
			if (desc.Extent.Width == 0 || desc.Extent.Height == 0 || desc.Extent.Depth == 0 ||
				desc.MipLevels == 0 || desc.ArrayLayers == 0 || desc.PixelFormat == Rhi::Format::Undefined ||
				desc.Usage == Rhi::TextureUsage::None)
			{
				return false;
			}
			if (ToVkFormat(desc.PixelFormat) == VK_FORMAT_UNDEFINED)
			{
				return false;
			}
			if (desc.Samples != Rhi::SampleCount::X1 && desc.MipLevels != 1)
			{
				return false;
			}
			if (Rhi::IsDepthFormat(desc.PixelFormat) && HasTextureUsage(desc.Usage, Rhi::TextureUsage::ColorAttachment))
			{
				return false;
			}
			if (!Rhi::IsDepthFormat(desc.PixelFormat) && HasTextureUsage(desc.Usage, Rhi::TextureUsage::DepthStencilAttachment))
			{
				return false;
			}

			switch (desc.Dimension)
			{
			case Rhi::TextureDimension::Texture1D:
				return desc.Extent.Height == 1 && desc.Extent.Depth == 1;
			case Rhi::TextureDimension::Texture2D:
				return desc.Extent.Depth == 1;
			case Rhi::TextureDimension::Texture3D:
				return desc.ArrayLayers == 1;
			case Rhi::TextureDimension::TextureCube:
				return desc.Extent.Depth == 1 && desc.Extent.Width == desc.Extent.Height &&
					desc.ArrayLayers >= 6 && (desc.ArrayLayers % 6) == 0;
			default:
				return false;
			}
		}

} // namespace Swim::RhiVulkan
