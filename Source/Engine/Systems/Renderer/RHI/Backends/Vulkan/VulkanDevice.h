#pragma once

#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Platform/Window.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandPool.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandPoolState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTexture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanFence.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanSemaphore.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanTimeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanSwapchain.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Swim::RhiVulkan
{

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

			std::unique_ptr<Rhi::CommandPool> CreateCommandPool(Rhi::QueueType queueType) override
			{
				std::uint32_t familyIndex = UINT32_MAX;
				switch (queueType)
				{
				case Rhi::QueueType::Graphics:
					familyIndex = state->QueueFamilies.Graphics;
					break;
				case Rhi::QueueType::Compute:
					familyIndex = state->QueueFamilies.Compute;
					break;
				case Rhi::QueueType::Transfer:
					familyIndex = state->QueueFamilies.Transfer;
					break;
				}
				if (familyIndex == UINT32_MAX)
				{
					return nullptr;
				}

				VkCommandPoolCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
				createInfo.queueFamilyIndex = familyIndex;

				VkCommandPool commandPool = VK_NULL_HANDLE;
				if (state->Dispatch.vkCreateCommandPool(
					state->Device.device, &createInfo, nullptr, &commandPool) != VK_SUCCESS)
				{
					return nullptr;
				}

				auto poolState = std::make_shared<VulkanCommandPoolState>();
				poolState->DeviceState = state;
				poolState->Pool = commandPool;
				poolState->FamilyIndex = familyIndex;
				return std::make_unique<VulkanCommandPool>(std::move(poolState));
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

			std::unique_ptr<Rhi::Timeline> CreateTimeline(std::uint64_t initialValue) override
			{
				VkSemaphoreTypeCreateInfo typeInfo{};
				typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
				typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
				typeInfo.initialValue = initialValue;

				VkSemaphoreCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
				createInfo.pNext = &typeInfo;

				VkSemaphore semaphore = VK_NULL_HANDLE;
				if (state->Dispatch.vkCreateSemaphore(state->Device.device, &createInfo, nullptr, &semaphore) != VK_SUCCESS)
				{
					return nullptr;
				}

				auto timelineState = std::make_shared<VulkanTimelineState>();
				timelineState->DeviceState = state;
				timelineState->Semaphore = semaphore;
				return std::make_unique<VulkanTimeline>(std::move(timelineState));
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

} // namespace Swim::RhiVulkan
