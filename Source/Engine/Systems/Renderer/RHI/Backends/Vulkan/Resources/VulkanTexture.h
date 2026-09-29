#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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
				RetireLostVulkanDevice(*state);

				for (const auto& [key, view] : views)
				{
					(void)key;
					state->Dispatch.vkDestroyImageView(state->Device.device, view, nullptr);
				}

				views.clear();

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

			// Views of this image by their creation parameters, created once and destroyed
			// with the image (render graphs ask for the same views every frame).
			using ViewKey = std::array<std::uint32_t, 7>;

			template <typename Create> VkImageView GetOrCreateView(const ViewKey& key, Create&& create)
			{
				std::lock_guard lock(viewMutex);

				for (const auto& [existing, view] : views)
				{
					if (existing == key)
					{
						return view;
					}
				}

				const VkImageView view = create();

				if (view != VK_NULL_HANDLE)
				{
					views.emplace_back(key, view);
				}

				return view;
			}

		private:

			std::shared_ptr<VulkanDeviceState> state;
			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			std::string debugName;
			Rhi::TextureDesc desc{};
			std::mutex viewMutex;
			std::vector<std::pair<ViewKey, VkImageView>> views;

		};

} // namespace Swim::RhiVulkan
