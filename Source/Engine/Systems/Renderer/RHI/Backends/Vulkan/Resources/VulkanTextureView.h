#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTexture.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <string>

namespace Swim::RhiVulkan
{

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

} // namespace Swim::RhiVulkan
