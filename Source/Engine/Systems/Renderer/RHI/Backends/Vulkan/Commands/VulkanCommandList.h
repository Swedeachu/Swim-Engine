#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandPoolState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <stdexcept>

namespace Swim::RhiVulkan
{

		class VulkanCommandList final : public Rhi::CommandList
		{
		public:
			VulkanCommandList(std::shared_ptr<VulkanCommandPoolState> poolState, VkCommandBuffer commandBuffer)
				: poolState(std::move(poolState)), commandBuffer(commandBuffer)
			{
			}

			~VulkanCommandList() override
			{
				if (commandBuffer != VK_NULL_HANDLE)
				{
					poolState->DeviceState->Dispatch.vkFreeCommandBuffers(
						poolState->DeviceState->Device.device, poolState->Pool, 1, &commandBuffer);
				}
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(commandBuffer);
			}

			void Begin() override
			{
				if (recording)
				{
					throw std::logic_error("Vulkan command list is already recording");
				}

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				if (poolState->DeviceState->Dispatch.vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to begin Vulkan command buffer");
				}
				recording = true;
			}

			void End() override
			{
				if (!recording)
				{
					throw std::logic_error("Vulkan command list is not recording");
				}
				if (poolState->DeviceState->Dispatch.vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to end Vulkan command buffer");
				}
				recording = false;
			}

			void Transition(Rhi::Buffer&, Rhi::ResourceState, Rhi::ResourceState) override
			{
				throw std::logic_error("Vulkan buffer transitions are implemented with the item 39 render smoke path");
			}

			void Transition(
				Rhi::Texture&,
				Rhi::ResourceState,
				Rhi::ResourceState,
				const Rhi::TextureSubresourceRange&) override
			{
				throw std::logic_error("Vulkan image transitions are implemented with the item 39 render smoke path");
			}

			void CopyBuffer(Rhi::Buffer&, Rhi::Buffer&, const Rhi::BufferCopyRegion&) override
			{
				throw std::logic_error("Vulkan buffer copies are implemented with the item 39 render smoke path");
			}

			void CopyTexture(Rhi::Texture&, Rhi::Texture&, const Rhi::TextureCopyRegion&) override
			{
				throw std::logic_error("Vulkan texture copies are implemented with the item 39 render smoke path");
			}

			void BeginRendering(const Rhi::RenderingDesc&) override
			{
				throw std::logic_error("Vulkan dynamic rendering commands are implemented with item 39");
			}

			void EndRendering() override
			{
				throw std::logic_error("Vulkan dynamic rendering commands are implemented with item 39");
			}

			void BindGraphicsPipeline(Rhi::GraphicsPipeline&) override
			{
				throw std::logic_error("Vulkan graphics pipelines are implemented with item 39");
			}

			void BindComputePipeline(Rhi::ComputePipeline&) override
			{
				throw std::logic_error("Vulkan compute pipelines are implemented with item 39");
			}

			void BindDescriptorTable(std::uint32_t, Rhi::DescriptorTable&) override
			{
				throw std::logic_error("Vulkan descriptor tables are implemented with item 39");
			}

			void SetViewport(const Rhi::Viewport&) override
			{
				throw std::logic_error("Vulkan viewport commands are implemented with item 39");
			}

			void SetScissor(const Rhi::ScissorRect&) override
			{
				throw std::logic_error("Vulkan scissor commands are implemented with item 39");
			}

			void BindVertexBuffer(std::uint32_t, Rhi::Buffer&, std::uint64_t) override
			{
				throw std::logic_error("Vulkan vertex binding is implemented with item 39");
			}

			void BindIndexBuffer(Rhi::Buffer&, std::uint64_t, Rhi::IndexType) override
			{
				throw std::logic_error("Vulkan index binding is implemented with item 39");
			}

			void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override
			{
				throw std::logic_error("Vulkan draw commands are implemented with item 39");
			}

			void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override
			{
				throw std::logic_error("Vulkan indexed draw commands are implemented with item 39");
			}

			void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override
			{
				throw std::logic_error("Vulkan dispatch commands are implemented with item 39");
			}

			void WriteTimestamp(Rhi::QueryPool&, std::uint32_t) override
			{
				throw std::logic_error("Vulkan timestamps are implemented with item 39");
			}

			VkCommandBuffer GetCommandBuffer() const
			{
				return commandBuffer;
			}

			std::uint32_t GetQueueFamilyIndex() const
			{
				return poolState->FamilyIndex;
			}

			const std::shared_ptr<VulkanDeviceState>& GetState() const
			{
				return poolState->DeviceState;
			}

		private:
			std::shared_ptr<VulkanCommandPoolState> poolState;
			VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
			bool recording = false;
		};

} // namespace Swim::RhiVulkan
