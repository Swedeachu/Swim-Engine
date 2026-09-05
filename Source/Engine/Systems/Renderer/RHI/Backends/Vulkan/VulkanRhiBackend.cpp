#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Platform/Window.h"

#include <volk.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Swim::RhiVulkan
{

	namespace
	{

		template <typename Handle>
		std::uintptr_t ToNativeHandle(Handle handle)
		{
			if constexpr (std::is_pointer_v<Handle>)
			{
				return reinterpret_cast<std::uintptr_t>(handle);
			}
			else
			{
				return static_cast<std::uintptr_t>(handle);
			}
		}

		template <typename Handle>
		Handle FromNativeHandle(std::uintptr_t handle)
		{
			if constexpr (std::is_pointer_v<Handle>)
			{
				return reinterpret_cast<Handle>(handle);
			}
			else
			{
				return static_cast<Handle>(handle);
			}
		}

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


		struct QueueFamilySelection
		{
			std::uint32_t Graphics = UINT32_MAX;
			std::uint32_t Compute = UINT32_MAX;
			std::uint32_t Transfer = UINT32_MAX;

			bool IsValid() const
			{
				return Graphics != UINT32_MAX && Compute != UINT32_MAX && Transfer != UINT32_MAX;
			}
		};

		QueueFamilySelection SelectQueueFamilies(
			VkInstance instance,
			const vkb::PhysicalDevice& physicalDevice)
		{
			QueueFamilySelection selection{};
			const auto families = physicalDevice.get_queue_families();

			for (std::uint32_t index = 0; index < families.size(); ++index)
			{
				const auto& family = families[index];
				if (family.queueCount == 0 || (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				{
					continue;
				}

				if (Platform::Internal::GetVulkanPresentationSupport(
					ToNativeHandle(instance), ToNativeHandle(physicalDevice.physical_device), index))
				{
					selection.Graphics = index;
					break;
				}
			}

			for (std::uint32_t index = 0; index < families.size(); ++index)
			{
				const auto& family = families[index];
				if (family.queueCount == 0 || (family.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
				{
					continue;
				}

				if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				{
					selection.Compute = index;
					break;
				}
			}
			if (selection.Compute == UINT32_MAX)
			{
				selection.Compute = selection.Graphics;
			}

			for (std::uint32_t index = 0; index < families.size(); ++index)
			{
				const auto& family = families[index];
				if (family.queueCount == 0 || (family.queueFlags & VK_QUEUE_TRANSFER_BIT) == 0)
				{
					continue;
				}

				if ((family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
				{
					selection.Transfer = index;
					break;
				}
			}
			if (selection.Transfer == UINT32_MAX)
			{
				for (std::uint32_t index = 0; index < families.size(); ++index)
				{
					const auto& family = families[index];
					if (family.queueCount > 0 &&
						(family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 &&
						(family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
					{
						selection.Transfer = index;
						break;
					}
				}
			}
			if (selection.Transfer == UINT32_MAX)
			{
				selection.Transfer = selection.Compute != UINT32_MAX ? selection.Compute : selection.Graphics;
			}

			return selection;
		}

		struct VulkanInstanceState
		{
			vkb::Instance Instance{};
			volk::VolkInstanceTable Dispatch{};
			bool LoaderAcquired = false;

			~VulkanInstanceState()
			{
				if (Instance.instance != VK_NULL_HANDLE)
				{
					vkb::destroy_instance(Instance);
				}
				if (LoaderAcquired)
				{
					Platform::Internal::ReleaseVulkanLoader();
				}
			}
		};

		struct VulkanDeviceState
		{
			std::shared_ptr<VulkanInstanceState> Instance;
			vkb::Device Device{};
			volk::VolkDeviceTable Dispatch{};
			QueueFamilySelection QueueFamilies{};
			VmaVulkanFunctions AllocatorFunctions{};
			VmaAllocator Allocator = nullptr;

			~VulkanDeviceState()
			{
				if (Allocator != nullptr)
				{
					vmaDestroyAllocator(Allocator);
				}
				if (Device.device != VK_NULL_HANDLE)
				{
					vkb::destroy_device(Device);
				}
			}
		};

		Rhi::AdapterInfo BuildAdapterInfo(
			const volk::VolkInstanceTable& dispatch,
			const vkb::PhysicalDevice& physicalDevice)
		{
			VkPhysicalDeviceVulkan12Features features12{};
			features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

			VkPhysicalDeviceVulkan13Features features13{};
			features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

			VkPhysicalDeviceFeatures2 features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			features.pNext = &features12;
			features12.pNext = &features13;

			void** featureTail = &features13.pNext;

#ifdef VK_EXT_descriptor_buffer
			VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures{};
			descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
			*featureTail = &descriptorBufferFeatures;
			featureTail = &descriptorBufferFeatures.pNext;
#endif

#ifdef VK_EXT_mesh_shader
			VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
			meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
			*featureTail = &meshShaderFeatures;
			featureTail = &meshShaderFeatures.pNext;
#endif

#ifdef VK_KHR_ray_query
			VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
			rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
			*featureTail = &rayQueryFeatures;
			featureTail = &rayQueryFeatures.pNext;
#endif

#ifdef VK_KHR_ray_tracing_pipeline
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
			rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			*featureTail = &rayTracingPipelineFeatures;
			featureTail = &rayTracingPipelineFeatures.pNext;
#endif

			*featureTail = nullptr;
			dispatch.vkGetPhysicalDeviceFeatures2(physicalDevice.physical_device, &features);

			VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties{};
			descriptorIndexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

			VkPhysicalDeviceSubgroupProperties subgroupProperties{};
			subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
			subgroupProperties.pNext = &descriptorIndexingProperties;

			VkPhysicalDeviceProperties2 properties{};
			properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			properties.pNext = &subgroupProperties;
			dispatch.vkGetPhysicalDeviceProperties2(physicalDevice.physical_device, &properties);

			VkPhysicalDeviceMemoryProperties memoryProperties{};
			dispatch.vkGetPhysicalDeviceMemoryProperties(physicalDevice.physical_device, &memoryProperties);

			const auto availableExtensions = physicalDevice.get_available_extensions();
			const auto& limits = properties.properties.limits;

			Rhi::AdapterInfo info{};
			info.Name = properties.properties.deviceName;
			info.VendorId = properties.properties.vendorID;
			info.DeviceId = properties.properties.deviceID;

			for (std::uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
			{
				const auto& heap = memoryProperties.memoryHeaps[heapIndex];
				if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
				{
					info.DedicatedVideoMemory += heap.size;
				}
			}

			auto& capabilities = info.Capabilities;
			capabilities.Descriptors.MaxSampledTexturesPerStage = limits.maxPerStageDescriptorSampledImages;
			capabilities.Descriptors.MaxSamplersPerStage = limits.maxPerStageDescriptorSamplers;
			capabilities.Descriptors.MaxStorageTexturesPerStage = limits.maxPerStageDescriptorStorageImages;
			capabilities.Descriptors.MaxUniformBuffersPerStage = limits.maxPerStageDescriptorUniformBuffers;
			capabilities.Descriptors.MaxStorageBuffersPerStage = limits.maxPerStageDescriptorStorageBuffers;
			capabilities.Descriptors.MaxBindlessSampledTextures = descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages;
			capabilities.Descriptors.MaxBindlessSamplers = descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSamplers;

			capabilities.Queues.DedicatedCompute = physicalDevice.has_dedicated_compute_queue();
			capabilities.Queues.DedicatedTransfer = physicalDevice.has_dedicated_transfer_queue();
			capabilities.Queues.AsyncCompute = physicalDevice.has_separate_compute_queue();

			capabilities.MaxPushConstantBytes = limits.maxPushConstantsSize;
			capabilities.MaxColorAttachments = limits.maxColorAttachments;
			capabilities.MaxSamples = GetMaximumSampleCount(
				limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts);
			capabilities.SubgroupSize = subgroupProperties.subgroupSize;
			capabilities.MinUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
			capabilities.MinStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
			if (limits.timestampComputeAndGraphics != VK_FALSE && limits.timestampPeriod > 0.0f)
			{
				capabilities.TimestampFrequency = static_cast<std::uint64_t>(
					std::llround(1'000'000'000.0 / static_cast<double>(limits.timestampPeriod)));
			}

			capabilities.DescriptorIndexing =
				features12.descriptorIndexing != VK_FALSE &&
				features12.runtimeDescriptorArray != VK_FALSE &&
				features12.descriptorBindingPartiallyBound != VK_FALSE;
			capabilities.BufferDeviceAddress = features12.bufferDeviceAddress != VK_FALSE;
			capabilities.IndirectCount = features12.drawIndirectCount != VK_FALSE;
			capabilities.SubgroupOperations = subgroupProperties.supportedOperations != 0;
			capabilities.TimestampQueries = limits.timestampComputeAndGraphics != VK_FALSE;
			capabilities.SparseResidency =
				features.features.sparseBinding != VK_FALSE &&
				(features.features.sparseResidencyBuffer != VK_FALSE ||
				 features.features.sparseResidencyImage2D != VK_FALSE ||
				 features.features.sparseResidencyImage3D != VK_FALSE);
			capabilities.BcTextureCompression = features.features.textureCompressionBC != VK_FALSE;
			capabilities.Etc2TextureCompression = features.features.textureCompressionETC2 != VK_FALSE;
			capabilities.AstcTextureCompression = features.features.textureCompressionASTC_LDR != VK_FALSE;

#ifdef VK_EXT_descriptor_buffer
			capabilities.DescriptorBuffer =
				HasExtension(availableExtensions, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) &&
				descriptorBufferFeatures.descriptorBuffer != VK_FALSE;
#endif
#ifdef VK_EXT_mesh_shader
			capabilities.MeshShaders =
				HasExtension(availableExtensions, VK_EXT_MESH_SHADER_EXTENSION_NAME) &&
				meshShaderFeatures.meshShader != VK_FALSE;
			capabilities.TaskShaders = capabilities.MeshShaders && meshShaderFeatures.taskShader != VK_FALSE;
#endif
#ifdef VK_KHR_ray_query
			capabilities.RayQuery =
				HasExtension(availableExtensions, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
				rayQueryFeatures.rayQuery != VK_FALSE;
#endif
#ifdef VK_KHR_ray_tracing_pipeline
			capabilities.RayTracingPipeline =
				HasExtension(availableExtensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
				rayTracingPipelineFeatures.rayTracingPipeline != VK_FALSE;
#endif
#ifdef VK_EXT_memory_budget
			capabilities.MemoryBudget = HasExtension(availableExtensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif

			return info;
		}

		class VulkanQueue final : public Rhi::Queue
		{
		public:
			VulkanQueue(
				std::shared_ptr<VulkanDeviceState> state,
				Rhi::QueueType type,
				VkQueue queue,
				std::uint32_t familyIndex)
				: state(std::move(state)), type(type), queue(queue), familyIndex(familyIndex)
			{
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(queue);
			}

			Rhi::QueueType GetType() const override
			{
				return type;
			}

			std::uint32_t GetFamilyIndex() const
			{
				return familyIndex;
			}

			VkQueue GetQueue() const
			{
				return queue;
			}

			void Submit(const Rhi::SubmitDesc& desc) override
			{
				(void)desc;
				std::terminate();
			}

			void WaitIdle() override
			{
				state->Dispatch.vkQueueWaitIdle(queue);
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			Rhi::QueueType type = Rhi::QueueType::Graphics;
			VkQueue queue = VK_NULL_HANDLE;
			std::uint32_t familyIndex = 0;
		};

		class VulkanBuffer final : public Rhi::Buffer
		{
		public:
			VulkanBuffer(
				std::shared_ptr<VulkanDeviceState> state,
				VkBuffer buffer,
				VmaAllocation allocation,
				Rhi::BufferDesc desc)
				: state(std::move(state)),
				  buffer(buffer),
				  allocation(allocation),
				  debugName(desc.DebugName),
				  desc(std::move(desc))
			{
				this->desc.DebugName = debugName;
			}

			~VulkanBuffer() override
			{
				if (buffer != VK_NULL_HANDLE && allocation != nullptr)
				{
					vmaDestroyBuffer(state->Allocator, buffer, allocation);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(buffer);
			}

			const Rhi::BufferDesc& GetDesc() const override
			{
				return desc;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			std::string debugName;
			Rhi::BufferDesc desc{};
		};

		class VulkanTexture final : public Rhi::Texture
		{
		public:
			VulkanTexture(
				std::shared_ptr<VulkanDeviceState> state,
				VkImage image,
				Rhi::TextureDesc desc,
				VmaAllocation allocation = nullptr)
				: state(std::move(state)),
				  image(image),
				  allocation(allocation),
				  debugName(desc.DebugName),
				  desc(std::move(desc))
			{
				this->desc.DebugName = debugName;
			}

			~VulkanTexture() override
			{
				if (image != VK_NULL_HANDLE && allocation != nullptr)
				{
					vmaDestroyImage(state->Allocator, image, allocation);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(image);
			}

			const Rhi::TextureDesc& GetDesc() const override
			{
				return desc;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			std::string debugName;
			Rhi::TextureDesc desc{};
		};

		class VulkanTextureView final : public Rhi::TextureView
		{
		public:
			VulkanTextureView(
				std::shared_ptr<VulkanDeviceState> state,
				VulkanTexture& texture,
				VkImageView view,
				Rhi::TextureViewDesc desc,
				bool ownsView = false)
				: state(std::move(state)),
				  texture(texture),
				  view(view),
				  ownsView(ownsView),
				  debugName(desc.DebugName),
				  desc(std::move(desc))
			{
				this->desc.DebugName = debugName;
			}

			~VulkanTextureView() override
			{
				if (ownsView && view != VK_NULL_HANDLE)
				{
					state->Dispatch.vkDestroyImageView(state->Device.device, view, nullptr);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(view);
			}

			Rhi::Texture& GetTexture() const override
			{
				return texture;
			}

			const Rhi::TextureViewDesc& GetDesc() const override
			{
				return desc;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VulkanTexture& texture;
			VkImageView view = VK_NULL_HANDLE;
			bool ownsView = false;
			std::string debugName;
			Rhi::TextureViewDesc desc{};
		};

		class VulkanSemaphore final : public Rhi::Semaphore
		{
		public:
			VulkanSemaphore(std::shared_ptr<VulkanDeviceState> state, VkSemaphore semaphore)
				: state(std::move(state)), semaphore(semaphore)
			{
			}

			~VulkanSemaphore() override
			{
				if (semaphore != VK_NULL_HANDLE)
				{
					state->Dispatch.vkDestroySemaphore(state->Device.device, semaphore, nullptr);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(semaphore);
			}

			VkSemaphore GetSemaphore() const
			{
				return semaphore;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkSemaphore semaphore = VK_NULL_HANDLE;
		};

		class VulkanFence final : public Rhi::Fence
		{
		public:
			VulkanFence(std::shared_ptr<VulkanDeviceState> state, VkFence fence)
				: state(std::move(state)), fence(fence)
			{
			}

			~VulkanFence() override
			{
				if (fence != VK_NULL_HANDLE)
				{
					state->Dispatch.vkDestroyFence(state->Device.device, fence, nullptr);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(fence);
			}

			bool IsSignaled() const override
			{
				return state->Dispatch.vkGetFenceStatus(state->Device.device, fence) == VK_SUCCESS;
			}

			bool Wait(std::uint64_t timeoutNanoseconds) override
			{
				const VkResult result = state->Dispatch.vkWaitForFences(
					state->Device.device, 1, &fence, VK_TRUE, timeoutNanoseconds);
				return result == VK_SUCCESS;
			}

			void Reset() override
			{
				if (state->Dispatch.vkResetFences(state->Device.device, 1, &fence) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to reset Vulkan fence");
				}
			}

			VkFence GetFence() const
			{
				return fence;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkFence fence = VK_NULL_HANDLE;
		};

		class VulkanSwapchain final : public Rhi::Swapchain
		{
		public:
			VulkanSwapchain(
				std::shared_ptr<VulkanDeviceState> state,
				Platform::Window& window,
				VkSurfaceKHR surface,
				Rhi::SwapchainDesc desc)
				: state(std::move(state)), window(window), surface(surface), desc(desc)
			{
			}

			~VulkanSwapchain() override
			{
				DestroySwapchain();
				if (surface != VK_NULL_HANDLE)
				{
					Platform::Internal::DestroyVulkanSurface(
						ToNativeHandle(state->Instance->Instance.instance), ToNativeHandle(surface));
					surface = VK_NULL_HANDLE;
				}
			}

			bool Initialize()
			{
				const Platform::Extent2D pixelSize = window.GetPixelSize();
				return Rebuild(pixelSize.Width, pixelSize.Height, false);
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(swapchain.swapchain);
			}

			Rhi::Format GetFormat() const override
			{
				return format;
			}

			Rhi::Extent2D GetExtent() const override
			{
				return extent;
			}

			std::uint32_t GetImageCount() const override
			{
				return static_cast<std::uint32_t>(views.size());
			}

			Rhi::TextureView& GetImageView(std::uint32_t imageIndex) override
			{
				return *views.at(imageIndex);
			}

			Rhi::SwapchainAcquireResult AcquireNextImage(
				Rhi::Semaphore& signalSemaphore,
				Rhi::Fence* signalFence) override
			{
				auto* semaphore = dynamic_cast<VulkanSemaphore*>(&signalSemaphore);
				auto* fence = signalFence ? dynamic_cast<VulkanFence*>(signalFence) : nullptr;
				if (!semaphore || (signalFence && !fence))
				{
					throw std::invalid_argument("Vulkan swapchain acquire requires Vulkan synchronization objects");
				}

				Rhi::SwapchainAcquireResult result{};
				const VkResult vkResult = state->Dispatch.vkAcquireNextImageKHR(
					state->Device.device,
					swapchain.swapchain,
					UINT64_MAX,
					semaphore->GetSemaphore(),
					fence ? fence->GetFence() : VK_NULL_HANDLE,
					&result.ImageIndex);

				result.OutOfDate = vkResult == VK_ERROR_OUT_OF_DATE_KHR;
				result.Suboptimal = vkResult == VK_SUBOPTIMAL_KHR;
				if (vkResult != VK_SUCCESS && !result.OutOfDate && !result.Suboptimal)
				{
					throw std::runtime_error("Failed to acquire Vulkan swapchain image");
				}
				return result;
			}

			bool Present(
				Rhi::Queue& queue,
				std::uint32_t imageIndex,
				std::span<Rhi::Semaphore* const> waits) override
			{
				auto* vulkanQueue = dynamic_cast<VulkanQueue*>(&queue);
				if (!vulkanQueue || vulkanQueue->GetFamilyIndex() != state->QueueFamilies.Graphics)
				{
					throw std::invalid_argument("Vulkan swapchain presentation requires the device graphics/present queue");
				}

				std::vector<VkSemaphore> waitSemaphores;
				waitSemaphores.reserve(waits.size());
				for (Rhi::Semaphore* wait : waits)
				{
					auto* semaphore = dynamic_cast<VulkanSemaphore*>(wait);
					if (!semaphore)
					{
						throw std::invalid_argument("Vulkan swapchain presentation requires Vulkan semaphores");
					}
					waitSemaphores.push_back(semaphore->GetSemaphore());
				}

				VkPresentInfoKHR presentInfo{};
				presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
				presentInfo.waitSemaphoreCount = static_cast<std::uint32_t>(waitSemaphores.size());
				presentInfo.pWaitSemaphores = waitSemaphores.data();
				presentInfo.swapchainCount = 1;
				presentInfo.pSwapchains = &swapchain.swapchain;
				presentInfo.pImageIndices = &imageIndex;

				const VkResult result = state->Dispatch.vkQueuePresentKHR(vulkanQueue->GetQueue(), &presentInfo);
				if (result == VK_ERROR_OUT_OF_DATE_KHR)
				{
					return false;
				}
				if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
				{
					throw std::runtime_error("Failed to present Vulkan swapchain image");
				}
				return true;
			}

			void Resize(Rhi::Extent2D requestedExtent) override
			{
				if (requestedExtent.Width == 0 || requestedExtent.Height == 0)
				{
					return;
				}
				if (!Rebuild(requestedExtent.Width, requestedExtent.Height, true))
				{
					throw std::runtime_error("Failed to resize Vulkan swapchain");
				}
			}

		private:
			bool Rebuild(std::uint32_t width, std::uint32_t height, bool replaceExisting)
			{
				if (width == 0 || height == 0 || desc.Hdr)
				{
					return false;
				}

				vkb::SwapchainBuilder builder{ state->Device, surface };
				if (replaceExisting && swapchain.swapchain != VK_NULL_HANDLE)
				{
					builder.set_old_swapchain(swapchain);
				}

				const VkFormat preferredFormat = ToVkFormat(desc.PreferredFormat);
				if (preferredFormat != VK_FORMAT_UNDEFINED)
				{
					builder.set_desired_format({ preferredFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
				}
				builder
					.add_fallback_format({ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
					.set_desired_extent(width, height)
					.set_desired_min_image_count(std::max(2u, desc.ImageCount))
					.set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

				if (desc.Vsync)
				{
					builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
				}
				else
				{
					builder
						.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
						.add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
						.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
				}

				auto swapchainResult = builder.build();
				if (!swapchainResult)
				{
					return false;
				}

				vkb::Swapchain newSwapchain = std::move(swapchainResult).value();
				auto imagesResult = newSwapchain.get_images();
				auto viewsResult = newSwapchain.get_image_views();
				if (!imagesResult || !viewsResult)
				{
					vkb::destroy_swapchain(newSwapchain);
					return false;
				}

				auto newImages = std::move(imagesResult).value();
				auto newViews = std::move(viewsResult).value();
				if (newImages.size() != newViews.size())
				{
					newSwapchain.destroy_image_views(newViews);
					vkb::destroy_swapchain(newSwapchain);
					return false;
				}

				if (replaceExisting)
				{
					state->Dispatch.vkDeviceWaitIdle(state->Device.device);
					DestroySwapchain();
				}

				swapchain = std::move(newSwapchain);
				imageViews = std::move(newViews);
				format = FromVkFormat(swapchain.image_format);
				extent = { swapchain.extent.width, swapchain.extent.height };

				textures.clear();
				views.clear();
				textures.reserve(newImages.size());
				views.reserve(newImages.size());
				for (std::size_t index = 0; index < newImages.size(); ++index)
				{
					Rhi::TextureDesc textureDesc{};
					textureDesc.Dimension = Rhi::TextureDimension::Texture2D;
					textureDesc.Extent = { extent.Width, extent.Height, 1 };
					textureDesc.PixelFormat = format;
					textureDesc.Usage = Rhi::TextureUsage::ColorAttachment;
					textures.push_back(std::make_unique<VulkanTexture>(state, newImages[index], textureDesc));

					Rhi::TextureViewDesc viewDesc{};
					viewDesc.Dimension = Rhi::TextureViewDimension::Texture2D;
					viewDesc.PixelFormat = format;
					views.push_back(std::make_unique<VulkanTextureView>(
						state, *textures.back(), imageViews[index], viewDesc));
				}
				return true;
			}

			void DestroySwapchain()
			{
				views.clear();
				textures.clear();
				if (!imageViews.empty() && swapchain.swapchain != VK_NULL_HANDLE)
				{
					swapchain.destroy_image_views(imageViews);
					imageViews.clear();
				}
				if (swapchain.swapchain != VK_NULL_HANDLE)
				{
					vkb::destroy_swapchain(swapchain);
					swapchain = {};
				}
			}

			std::shared_ptr<VulkanDeviceState> state;
			Platform::Window& window;
			VkSurfaceKHR surface = VK_NULL_HANDLE;
			Rhi::SwapchainDesc desc{};
			vkb::Swapchain swapchain{};
			std::vector<VkImageView> imageViews;
			std::vector<std::unique_ptr<VulkanTexture>> textures;
			std::vector<std::unique_ptr<VulkanTextureView>> views;
			Rhi::Format format = Rhi::Format::Undefined;
			Rhi::Extent2D extent{};
		};

		class VulkanDevice final : public Rhi::Device
		{
		public:
			VulkanDevice(
				std::shared_ptr<VulkanDeviceState> state,
				Rhi::AdapterInfo adapterInfo,
				std::unique_ptr<VulkanQueue> graphicsQueue,
				std::unique_ptr<VulkanQueue> computeQueue,
				std::unique_ptr<VulkanQueue> transferQueue)
				: state(std::move(state)),
				  adapterInfo(std::move(adapterInfo)),
				  graphicsQueue(std::move(graphicsQueue)),
				  computeQueue(std::move(computeQueue)),
				  transferQueue(std::move(transferQueue))
			{
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(state->Device.device);
			}

			const Rhi::AdapterInfo& GetAdapterInfo() const override
			{
				return adapterInfo;
			}

			Rhi::Queue& GetQueue(Rhi::QueueType type) override
			{
				switch (type)
				{
				case Rhi::QueueType::Graphics:
					return *graphicsQueue;
				case Rhi::QueueType::Compute:
					return *computeQueue;
				case Rhi::QueueType::Transfer:
					return *transferQueue;
				}
				return *graphicsQueue;
			}

			std::unique_ptr<Rhi::Swapchain> CreateSwapchain(
				Platform::Window& window,
				const Rhi::SwapchainDesc& desc) override
			{
				std::uintptr_t surfaceHandle = 0;
				if (!Platform::Internal::CreateVulkanSurface(
					window, ToNativeHandle(state->Instance->Instance.instance), surfaceHandle))
				{
					return nullptr;
				}
				const VkSurfaceKHR surface = FromNativeHandle<VkSurfaceKHR>(surfaceHandle);

				VkBool32 presentationSupported = VK_FALSE;
				const VkResult supportResult = state->Instance->Dispatch.vkGetPhysicalDeviceSurfaceSupportKHR(
					state->Device.physical_device.physical_device,
					state->QueueFamilies.Graphics,
					surface,
					&presentationSupported);
				if (supportResult != VK_SUCCESS || presentationSupported == VK_FALSE)
				{
					Platform::Internal::DestroyVulkanSurface(
						ToNativeHandle(state->Instance->Instance.instance), ToNativeHandle(surface));
					return nullptr;
				}

				auto result = std::make_unique<VulkanSwapchain>(state, window, surface, desc);
				if (!result->Initialize())
				{
					return nullptr;
				}
				return result;
			}

			std::unique_ptr<Rhi::Buffer> CreateBuffer(const Rhi::BufferDesc& desc) override
			{
				if (desc.Size == 0 || desc.Usage == Rhi::BufferUsage::None)
				{
					return nullptr;
				}

				const VkBufferUsageFlags usage = ToVkBufferUsage(desc.Usage);
				if (usage == 0)
				{
					return nullptr;
				}

				VkBufferCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				createInfo.size = desc.Size;
				createInfo.usage = usage;
				createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				VmaAllocationCreateInfo allocationInfo{};
				switch (desc.Memory)
				{
				case Rhi::MemoryPreference::DeviceLocal:
					allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
					break;
				case Rhi::MemoryPreference::CpuToGpu:
					allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
					allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
					break;
				case Rhi::MemoryPreference::GpuToCpu:
					allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
					allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
					break;
				default:
					return nullptr;
				}

				VkBuffer buffer = VK_NULL_HANDLE;
				VmaAllocation allocation = nullptr;
				if (vmaCreateBuffer(
					state->Allocator, &createInfo, &allocationInfo, &buffer, &allocation, nullptr) != VK_SUCCESS)
				{
					return nullptr;
				}

				if (!desc.DebugName.empty())
				{
					const std::string debugName(desc.DebugName);
					vmaSetAllocationName(state->Allocator, allocation, debugName.c_str());
				}
				return std::make_unique<VulkanBuffer>(state, buffer, allocation, desc);
			}

			std::unique_ptr<Rhi::Texture> CreateTexture(const Rhi::TextureDesc& desc) override
			{
				if (!ValidateTextureDesc(desc))
				{
					return nullptr;
				}

				VkImageCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				createInfo.flags = desc.Dimension == Rhi::TextureDimension::TextureCube
					? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
					: 0;
				createInfo.imageType = ToVkImageType(desc.Dimension);
				createInfo.format = ToVkFormat(desc.PixelFormat);
				createInfo.extent = { desc.Extent.Width, desc.Extent.Height, desc.Extent.Depth };
				createInfo.mipLevels = desc.MipLevels;
				createInfo.arrayLayers = desc.ArrayLayers;
				createInfo.samples = ToVkSampleCount(desc.Samples);
				createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
				createInfo.usage = ToVkImageUsage(desc.Usage);
				createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				if (createInfo.usage == 0)
				{
					return nullptr;
				}

				VmaAllocationCreateInfo allocationInfo{};
				allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

				VkImage image = VK_NULL_HANDLE;
				VmaAllocation allocation = nullptr;
				if (vmaCreateImage(
					state->Allocator, &createInfo, &allocationInfo, &image, &allocation, nullptr) != VK_SUCCESS)
				{
					return nullptr;
				}

				if (!desc.DebugName.empty())
				{
					const std::string debugName(desc.DebugName);
					vmaSetAllocationName(state->Allocator, allocation, debugName.c_str());
				}
				return std::make_unique<VulkanTexture>(state, image, desc, allocation);
			}

			std::unique_ptr<Rhi::TextureView> CreateTextureView(
				Rhi::Texture& texture,
				const Rhi::TextureViewDesc& desc) override
			{
				auto* vulkanTexture = dynamic_cast<VulkanTexture*>(&texture);
				if (vulkanTexture == nullptr)
				{
					return nullptr;
				}

				const Rhi::TextureDesc& textureDesc = vulkanTexture->GetDesc();
				const Rhi::Format viewFormat = desc.PixelFormat == Rhi::Format::Undefined
					? textureDesc.PixelFormat
					: desc.PixelFormat;
				if (viewFormat != textureDesc.PixelFormat || ToVkFormat(viewFormat) == VK_FORMAT_UNDEFINED ||
					desc.MipLevelCount == 0 || desc.ArrayLayerCount == 0 ||
					desc.BaseMipLevel >= textureDesc.MipLevels ||
					desc.MipLevelCount > textureDesc.MipLevels - desc.BaseMipLevel ||
					desc.BaseArrayLayer >= textureDesc.ArrayLayers ||
					desc.ArrayLayerCount > textureDesc.ArrayLayers - desc.BaseArrayLayer)
				{
					return nullptr;
				}

				switch (textureDesc.Dimension)
				{
				case Rhi::TextureDimension::Texture1D:
					if (desc.Dimension != Rhi::TextureViewDimension::Texture1D &&
						desc.Dimension != Rhi::TextureViewDimension::Texture1DArray)
					{
						return nullptr;
					}
					break;
				case Rhi::TextureDimension::Texture2D:
					if (desc.Dimension != Rhi::TextureViewDimension::Texture2D &&
						desc.Dimension != Rhi::TextureViewDimension::Texture2DArray)
					{
						return nullptr;
					}
					break;
				case Rhi::TextureDimension::Texture3D:
					if (desc.Dimension != Rhi::TextureViewDimension::Texture3D)
					{
						return nullptr;
					}
					break;
				case Rhi::TextureDimension::TextureCube:
					if (desc.Dimension != Rhi::TextureViewDimension::Texture2D &&
						desc.Dimension != Rhi::TextureViewDimension::Texture2DArray &&
						desc.Dimension != Rhi::TextureViewDimension::TextureCube &&
						desc.Dimension != Rhi::TextureViewDimension::TextureCubeArray)
					{
						return nullptr;
					}
					if (desc.Dimension == Rhi::TextureViewDimension::TextureCube && desc.ArrayLayerCount != 6)
					{
						return nullptr;
					}
					if (desc.Dimension == Rhi::TextureViewDimension::TextureCubeArray &&
						(desc.ArrayLayerCount % 6) != 0)
					{
						return nullptr;
					}
					break;
				default:
					return nullptr;
				}

				VkImageViewCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				createInfo.image = FromNativeHandle<VkImage>(vulkanTexture->GetNativeHandle());
				createInfo.viewType = ToVkImageViewType(desc.Dimension);
				createInfo.format = ToVkFormat(viewFormat);
				createInfo.subresourceRange.aspectMask = GetImageAspectMask(viewFormat);
				createInfo.subresourceRange.baseMipLevel = desc.BaseMipLevel;
				createInfo.subresourceRange.levelCount = desc.MipLevelCount;
				createInfo.subresourceRange.baseArrayLayer = desc.BaseArrayLayer;
				createInfo.subresourceRange.layerCount = desc.ArrayLayerCount;

				VkImageView view = VK_NULL_HANDLE;
				if (state->Dispatch.vkCreateImageView(state->Device.device, &createInfo, nullptr, &view) != VK_SUCCESS)
				{
					return nullptr;
				}

				Rhi::TextureViewDesc resolvedDesc = desc;
				resolvedDesc.PixelFormat = viewFormat;
				return std::make_unique<VulkanTextureView>(state, *vulkanTexture, view, resolvedDesc, true);
			}

			std::unique_ptr<Rhi::Sampler> CreateSampler(const Rhi::SamplerDesc&) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::ShaderProgram> CreateShaderProgram(const Rhi::ShaderProgramDesc&) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::PipelineLayout> CreatePipelineLayout(const Rhi::PipelineLayoutDesc&) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::GraphicsPipeline> CreateGraphicsPipeline(const Rhi::GraphicsPipelineDesc&) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::ComputePipeline> CreateComputePipeline(const Rhi::ComputePipelineDesc&) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::DescriptorTable> CreateDescriptorTable(const Rhi::DescriptorTableDesc&) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::CommandPool> CreateCommandPool(Rhi::QueueType) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::Semaphore> CreateSemaphore() override
			{
				VkSemaphoreCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

				VkSemaphore semaphore = VK_NULL_HANDLE;
				if (state->Dispatch.vkCreateSemaphore(state->Device.device, &createInfo, nullptr, &semaphore) != VK_SUCCESS)
				{
					return nullptr;
				}
				return std::make_unique<VulkanSemaphore>(state, semaphore);
			}

			std::unique_ptr<Rhi::Fence> CreateFence(bool signaled) override
			{
				VkFenceCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				createInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

				VkFence fence = VK_NULL_HANDLE;
				if (state->Dispatch.vkCreateFence(state->Device.device, &createInfo, nullptr, &fence) != VK_SUCCESS)
				{
					return nullptr;
				}
				return std::make_unique<VulkanFence>(state, fence);
			}

			std::unique_ptr<Rhi::Timeline> CreateTimeline(std::uint64_t) override
			{
				return nullptr;
			}

			std::unique_ptr<Rhi::QueryPool> CreateQueryPool(const Rhi::QueryPoolDesc&) override
			{
				return nullptr;
			}

			void WaitIdle() override
			{
				state->Dispatch.vkDeviceWaitIdle(state->Device.device);
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			Rhi::AdapterInfo adapterInfo;
			std::unique_ptr<VulkanQueue> graphicsQueue;
			std::unique_ptr<VulkanQueue> computeQueue;
			std::unique_ptr<VulkanQueue> transferQueue;
		};

		class VulkanAdapter final : public Rhi::Adapter
		{
		public:
			VulkanAdapter(
				std::shared_ptr<VulkanInstanceState> instance,
				vkb::PhysicalDevice physicalDevice,
				QueueFamilySelection queueFamilies)
				: instance(std::move(instance)),
				  physicalDevice(std::move(physicalDevice)),
				  queueFamilies(queueFamilies),
				  info(BuildAdapterInfo(this->instance->Dispatch, this->physicalDevice))
			{
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(physicalDevice.physical_device);
			}

			const Rhi::AdapterInfo& GetInfo() const override
			{
				return info;
			}

			std::unique_ptr<Rhi::Device> CreateDevice() override
			{
				if (!queueFamilies.IsValid())
				{
					return nullptr;
				}

				std::vector<vkb::CustomQueueDescription> queueDescriptions;
				std::vector<std::uint32_t> uniqueFamilies;
				for (std::uint32_t family : { queueFamilies.Graphics, queueFamilies.Compute, queueFamilies.Transfer })
				{
					if (std::find(uniqueFamilies.begin(), uniqueFamilies.end(), family) == uniqueFamilies.end())
					{
						uniqueFamilies.push_back(family);
						queueDescriptions.emplace_back(family, std::vector<float>{ 1.0f });
					}
				}

				auto deviceResult = vkb::DeviceBuilder{ physicalDevice }
					.custom_queue_setup(queueDescriptions)
					.build();
				if (!deviceResult)
				{
					return nullptr;
				}

				auto deviceState = std::make_shared<VulkanDeviceState>();
				deviceState->Instance = instance;
				deviceState->Device = std::move(deviceResult).value();
				deviceState->QueueFamilies = queueFamilies;
				volk::volkLoadDeviceTable(&deviceState->Dispatch, deviceState->Device.device);

				deviceState->AllocatorFunctions.vkGetInstanceProcAddr = deviceState->Instance->Instance.fp_vkGetInstanceProcAddr;
				deviceState->AllocatorFunctions.vkGetDeviceProcAddr = deviceState->Device.fp_vkGetDeviceProcAddr;

				VmaAllocatorCreateInfo allocatorInfo{};
				allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
				if (info.Capabilities.MemoryBudget)
				{
					allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
				}
				allocatorInfo.physicalDevice = deviceState->Device.physical_device.physical_device;
				allocatorInfo.device = deviceState->Device.device;
				allocatorInfo.instance = deviceState->Instance->Instance.instance;
				allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
				allocatorInfo.pVulkanFunctions = &deviceState->AllocatorFunctions;
				if (vmaCreateAllocator(&allocatorInfo, &deviceState->Allocator) != VK_SUCCESS)
				{
					return nullptr;
				}

				VkQueue graphicsQueueHandle = VK_NULL_HANDLE;
				VkQueue computeQueueHandle = VK_NULL_HANDLE;
				VkQueue transferQueueHandle = VK_NULL_HANDLE;
				deviceState->Dispatch.vkGetDeviceQueue(deviceState->Device.device, queueFamilies.Graphics, 0, &graphicsQueueHandle);
				deviceState->Dispatch.vkGetDeviceQueue(deviceState->Device.device, queueFamilies.Compute, 0, &computeQueueHandle);
				deviceState->Dispatch.vkGetDeviceQueue(deviceState->Device.device, queueFamilies.Transfer, 0, &transferQueueHandle);
				if (graphicsQueueHandle == VK_NULL_HANDLE || computeQueueHandle == VK_NULL_HANDLE || transferQueueHandle == VK_NULL_HANDLE)
				{
					return nullptr;
				}

				auto graphicsQueue = std::make_unique<VulkanQueue>(
					deviceState, Rhi::QueueType::Graphics, graphicsQueueHandle, queueFamilies.Graphics);
				auto computeQueue = std::make_unique<VulkanQueue>(
					deviceState, Rhi::QueueType::Compute, computeQueueHandle, queueFamilies.Compute);
				auto transferQueue = std::make_unique<VulkanQueue>(
					deviceState, Rhi::QueueType::Transfer, transferQueueHandle, queueFamilies.Transfer);

				return std::make_unique<VulkanDevice>(
					std::move(deviceState),
					info,
					std::move(graphicsQueue),
					std::move(computeQueue),
					std::move(transferQueue));
			}

		private:
			std::shared_ptr<VulkanInstanceState> instance;
			vkb::PhysicalDevice physicalDevice{};
			QueueFamilySelection queueFamilies{};
			Rhi::AdapterInfo info{};
		};

		class VulkanGraphicsSystem final : public Rhi::GraphicsSystem
		{
		public:
			VulkanGraphicsSystem(
				std::shared_ptr<VulkanInstanceState> instance,
				std::vector<std::unique_ptr<VulkanAdapter>> adapters)
				: instance(std::move(instance)), adapters(std::move(adapters))
			{
			}

			std::uint32_t GetAdapterCount() const override
			{
				return static_cast<std::uint32_t>(adapters.size());
			}

			Rhi::Adapter& GetAdapter(std::uint32_t adapterIndex) override
			{
				return *adapters.at(adapterIndex);
			}

		private:
			std::shared_ptr<VulkanInstanceState> instance;
			std::vector<std::unique_ptr<VulkanAdapter>> adapters;
		};

		VkPhysicalDeviceVulkan12Features GetRequiredVulkan12Features()
		{
			VkPhysicalDeviceVulkan12Features features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			features.drawIndirectCount = VK_TRUE;
			features.descriptorIndexing = VK_TRUE;
			features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
			features.descriptorBindingPartiallyBound = VK_TRUE;
			features.descriptorBindingVariableDescriptorCount = VK_TRUE;
			features.runtimeDescriptorArray = VK_TRUE;
			features.bufferDeviceAddress = VK_TRUE;
			features.timelineSemaphore = VK_TRUE;
			return features;
		}

		VkPhysicalDeviceVulkan13Features GetRequiredVulkan13Features()
		{
			VkPhysicalDeviceVulkan13Features features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			features.synchronization2 = VK_TRUE;
			features.dynamicRendering = VK_TRUE;
			return features;
		}

	} // namespace

	std::unique_ptr<Rhi::GraphicsSystem> CreateGraphicsSystem()
	{
		if (!Platform::Internal::AcquireVulkanLoader())
		{
			return nullptr;
		}

		auto instance = std::make_shared<VulkanInstanceState>();
		instance->LoaderAcquired = true;

		auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			Platform::Internal::GetVulkanInstanceProcAddress());
		if (!getInstanceProcAddr)
		{
			return nullptr;
		}

		const auto requiredExtensions = Platform::Internal::GetVulkanInstanceExtensions();
		if (requiredExtensions.empty())
		{
			return nullptr;
		}

		volk::volkInitializeCustom(getInstanceProcAddr);

		vkb::InstanceBuilder instanceBuilder{ getInstanceProcAddr };
		instanceBuilder
			.set_app_name("Swim Engine")
			.set_engine_name("Swim Engine")
			.require_api_version(1, 3, 0)
			.set_headless(true);

		for (const char* extension : requiredExtensions)
		{
			instanceBuilder.enable_extension(extension);
		}

#if defined(SWIM_VULKAN_VALIDATION)
		instanceBuilder
			.request_validation_layers(true)
			.use_default_debug_messenger();
#endif

		auto instanceResult = instanceBuilder.build();
		if (!instanceResult)
		{
			return nullptr;
		}

		instance->Instance = std::move(instanceResult).value();
		volk::volkLoadInstanceTable(&instance->Dispatch, instance->Instance.instance);

		auto selector = vkb::PhysicalDeviceSelector{ instance->Instance };
		selector
			.require_present(false)
			.set_minimum_version(1, 3)
			.add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
			.set_required_features_12(GetRequiredVulkan12Features())
			.set_required_features_13(GetRequiredVulkan13Features())
			.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
			.allow_any_gpu_device_type(true);

		auto physicalDevicesResult = selector.select_devices();
		if (!physicalDevicesResult)
		{
			return nullptr;
		}

		auto physicalDevices = std::move(physicalDevicesResult).value();
		if (physicalDevices.empty())
		{
			return nullptr;
		}

		std::vector<std::unique_ptr<VulkanAdapter>> adapters;
		adapters.reserve(physicalDevices.size());
		for (auto& physicalDevice : physicalDevices)
		{
			const QueueFamilySelection queueFamilies = SelectQueueFamilies(
				instance->Instance.instance, physicalDevice);
			if (!queueFamilies.IsValid())
			{
				continue;
			}

#ifdef VK_EXT_memory_budget
			physicalDevice.enable_extension_if_present(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif
			adapters.push_back(std::make_unique<VulkanAdapter>(
				instance, std::move(physicalDevice), queueFamilies));
		}

		if (adapters.empty())
		{
			return nullptr;
		}

		return std::make_unique<VulkanGraphicsSystem>(std::move(instance), std::move(adapters));
	}

	bool RegisterGraphicsBackend(Rhi::GraphicsFactory& factory)
	{
		return factory.Register(Rhi::GraphicsApi::Vulkan, &CreateGraphicsSystem);
	}

} // namespace Swim::RhiVulkan
