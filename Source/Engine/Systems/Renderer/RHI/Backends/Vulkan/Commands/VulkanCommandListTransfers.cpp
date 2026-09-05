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
		// This baseline records resource work on the graphics family. Dedicated
		// transfer/compute ownership and granularity policy arrive with transfers.
		RequireGraphicsQueue();
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

	void VulkanCommandList::Transition(Rhi::Texture& texture, Rhi::ResourceState before, Rhi::ResourceState after,
		const Rhi::TextureSubresourceRange& range)
	{
		RequireRecording(true);
		RequireGraphicsQueue();
		auto& native = RequireResource<VulkanTexture>(texture, GetState());
		if (after == Rhi::ResourceState::Undefined ||
			((before == Rhi::ResourceState::Present || after == Rhi::ResourceState::Present) && !native.IsSwapchainImage()))
		{
			throw std::invalid_argument("Undefined is source-only; Present requires a swapchain image");
		}
		const auto source = GetTextureState(texture.GetDesc(), before);
		const auto destination = GetTextureState(texture.GetDesc(), after);
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = source.Stages;
		barrier.srcAccessMask = source.Access;
		barrier.dstStageMask = destination.Stages;
		barrier.dstAccessMask = destination.Access;
		barrier.oldLayout = source.Layout;
		barrier.newLayout = destination.Layout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = FromNativeHandle<VkImage>(texture.GetNativeHandle());
		barrier.subresourceRange = GetSubresourceRange(texture.GetDesc(), range);
		VkDependencyInfo dependency{};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;
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

	void VulkanCommandList::CopyTexture(Rhi::Texture& source, Rhi::Texture& destination, const Rhi::TextureCopyRegion& region)
	{
		RequireRecording(true);
		RequireGraphicsQueue();
		RequireResource<VulkanTexture>(source, GetState());
		RequireResource<VulkanTexture>(destination, GetState());
		const auto& sourceDesc = source.GetDesc();
		const auto& destinationDesc = destination.GetDesc();
		if (&source == &destination || sourceDesc.PixelFormat != destinationDesc.PixelFormat ||
			sourceDesc.Dimension != destinationDesc.Dimension || sourceDesc.Samples != destinationDesc.Samples ||
			!HasTextureUsage(sourceDesc.Usage, Rhi::TextureUsage::TransferSource) ||
			!HasTextureUsage(destinationDesc.Usage, Rhi::TextureUsage::TransferDestination))
		{
			throw std::invalid_argument("Vulkan image copy requires distinct matching textures with transfer usage");
		}
		GetColorTexelBytes(sourceDesc.PixelFormat);
		ValidateCopyExtent(sourceDesc, region.Source, region.SourceOffset, region.Extent);
		ValidateCopyExtent(destinationDesc, region.Destination, region.DestinationOffset, region.Extent);
		VkImageCopy copy{};
		copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, region.Source.MipLevel, region.Source.ArrayLayer, 1 };
		copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, region.Destination.MipLevel, region.Destination.ArrayLayer, 1 };
		copy.srcOffset = { region.SourceOffset.X, region.SourceOffset.Y, region.SourceOffset.Z };
		copy.dstOffset = { region.DestinationOffset.X, region.DestinationOffset.Y, region.DestinationOffset.Z };
		copy.extent = { region.Extent.Width, region.Extent.Height, region.Extent.Depth };
		GetState()->Dispatch.vkCmdCopyImage(commandBuffer, FromNativeHandle<VkImage>(source.GetNativeHandle()),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FromNativeHandle<VkImage>(destination.GetNativeHandle()),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	}

	void VulkanCommandList::CopyBufferToTexture(Rhi::Buffer& source, Rhi::Texture& destination, const Rhi::BufferTextureCopyRegion& region)
	{
		RequireRecording(true);
		RequireGraphicsQueue();
		RequireResource<VulkanBuffer>(source, GetState());
		RequireResource<VulkanTexture>(destination, GetState());
		if (!HasBufferUsage(source.GetDesc().Usage, Rhi::BufferUsage::TransferSource) ||
			!HasTextureUsage(destination.GetDesc().Usage, Rhi::TextureUsage::TransferDestination))
		{
			throw std::invalid_argument("Vulkan upload requires transfer source/destination usage");
		}
		const auto copy = GetBufferImageCopy(source.GetDesc(), destination.GetDesc(), region);
		GetState()->Dispatch.vkCmdCopyBufferToImage(commandBuffer, FromNativeHandle<VkBuffer>(source.GetNativeHandle()),
			FromNativeHandle<VkImage>(destination.GetNativeHandle()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	}

	void VulkanCommandList::CopyTextureToBuffer(Rhi::Texture& source, Rhi::Buffer& destination, const Rhi::BufferTextureCopyRegion& region)
	{
		RequireRecording(true);
		RequireGraphicsQueue();
		RequireResource<VulkanTexture>(source, GetState());
		RequireResource<VulkanBuffer>(destination, GetState());
		if (!HasTextureUsage(source.GetDesc().Usage, Rhi::TextureUsage::TransferSource) ||
			!HasBufferUsage(destination.GetDesc().Usage, Rhi::BufferUsage::TransferDestination))
		{
			throw std::invalid_argument("Vulkan readback requires transfer source/destination usage");
		}
		const auto copy = GetBufferImageCopy(destination.GetDesc(), source.GetDesc(), region);
		GetState()->Dispatch.vkCmdCopyImageToBuffer(commandBuffer, FromNativeHandle<VkImage>(source.GetNativeHandle()),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FromNativeHandle<VkBuffer>(destination.GetNativeHandle()), 1, &copy);
	}

} // namespace Swim::RhiVulkan
