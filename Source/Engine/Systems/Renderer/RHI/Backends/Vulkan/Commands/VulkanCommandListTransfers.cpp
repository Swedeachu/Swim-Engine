#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTexture.h"

namespace Swim::RhiVulkan
{

	void VulkanCommandList::Transition(Rhi::Buffer& buffer, Rhi::ResourceState before, Rhi::ResourceState after)
	{
		RequireRecording(true);
		// Buffers remain on one family; barriers do not transfer queue ownership.
		if (poolState->FamilyIndex != GetState()->QueueFamilies.Graphics)
		{
			RequireComputeQueue();
			const auto unsupported = Rhi::ResourceState::VertexBuffer | Rhi::ResourceState::IndexBuffer;
			if ((static_cast<std::uint32_t>(before | after) & static_cast<std::uint32_t>(unsupported)) != 0)
			{
				throw std::invalid_argument("Compute-family buffer barriers cannot use vertex/index input states");
			}
		}
		RequireResource<VulkanBuffer>(buffer, GetState());
		if (after == Rhi::ResourceState::Undefined)
		{
			throw std::invalid_argument("Cannot transition a Vulkan buffer to Undefined");
		}
		const auto source = GetBufferState(buffer.GetDesc(), before);
		const auto destination = GetBufferState(buffer.GetDesc(), after);
		VkBufferMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		barrier.srcStageMask = source.Stages;
		barrier.srcAccessMask = source.Access;
		barrier.dstStageMask = destination.Stages;
		barrier.dstAccessMask = destination.Access;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = FromNativeHandle<VkBuffer>(buffer.GetNativeHandle());
		barrier.size = VK_WHOLE_SIZE;
		VkDependencyInfo dependency{};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.bufferMemoryBarrierCount = 1;
		dependency.pBufferMemoryBarriers = &barrier;
		GetState()->Dispatch.vkCmdPipelineBarrier2(commandBuffer, &dependency);
	}

	void VulkanCommandList::CopyBuffer(Rhi::Buffer& source, Rhi::Buffer& destination, const Rhi::BufferCopyRegion& region)
	{
		RequireRecording(true);
		RequireResource<VulkanBuffer>(source, GetState());
		RequireResource<VulkanBuffer>(destination, GetState());
		const auto& sourceDesc = source.GetDesc();
		const auto& destinationDesc = destination.GetDesc();
		if (!HasBufferUsage(sourceDesc.Usage, Rhi::BufferUsage::TransferSource) ||
			!HasBufferUsage(destinationDesc.Usage, Rhi::BufferUsage::TransferDestination) || region.Size == 0 ||
			region.SourceOffset > sourceDesc.Size || region.Size > sourceDesc.Size - region.SourceOffset ||
			region.DestinationOffset > destinationDesc.Size || region.Size > destinationDesc.Size - region.DestinationOffset)
		{
			throw std::invalid_argument("Vulkan buffer copy requires transfer usage and nonempty in-bounds ranges");
		}
		if (&source == &destination && region.SourceOffset < region.DestinationOffset + region.Size &&
			region.DestinationOffset < region.SourceOffset + region.Size)
		{
			throw std::invalid_argument("Vulkan buffer copy ranges must not overlap");
		}
		const VkBufferCopy copy{ region.SourceOffset, region.DestinationOffset, region.Size };
		GetState()->Dispatch.vkCmdCopyBuffer(commandBuffer, FromNativeHandle<VkBuffer>(source.GetNativeHandle()),
			FromNativeHandle<VkBuffer>(destination.GetNativeHandle()), 1, &copy);
	}

} // namespace Swim::RhiVulkan
