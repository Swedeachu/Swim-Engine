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

			~VulkanBuffer() override;
			void Write(std::uint64_t offset, std::span<const std::byte> data) override;
			void Read(std::uint64_t offset, std::span<std::byte> data) override;

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(buffer);
			}

			const Rhi::BufferDesc& GetDesc() const override
			{
				return desc;
			}

			const std::shared_ptr<VulkanDeviceState>& GetState() const
			{
				return state;
			}

		private:
			std::shared_ptr<VulkanDeviceState> state;
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			std::string debugName;
			Rhi::BufferDesc desc{};
		};

} // namespace Swim::RhiVulkan
