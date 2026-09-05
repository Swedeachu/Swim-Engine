#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <memory>
#include <string>

namespace Swim::RhiVulkan
{

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
				SetVulkanObjectName(*this->state, VK_OBJECT_TYPE_IMAGE, ToNativeHandle(image), debugName);
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

			const std::shared_ptr<VulkanDeviceState>& GetState() const
			{
				return state;
			}

			bool IsSwapchainImage() const
			{
				return allocation == nullptr;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			std::string debugName;
			Rhi::TextureDesc desc{};
		};

} // namespace Swim::RhiVulkan
