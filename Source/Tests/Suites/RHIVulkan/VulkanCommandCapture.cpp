#include "Tests/Fixtures/VulkanCommandCapture.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanCommandCapture* capture = nullptr;
	}

	VulkanCommandCapture::VulkanCommandCapture()
		: State(std::make_shared<RhiVulkan::VulkanDeviceState>()),
		  Pool(std::make_shared<RhiVulkan::VulkanCommandPoolState>())
	{
		capture = this;
		State->QueueFamilies.Graphics = 0;
		auto& limits = State->Device.physical_device.properties.limits;
		limits.maxColorAttachments = 8;
		limits.maxFramebufferWidth = limits.maxFramebufferHeight = 4096;
		limits.maxViewportDimensions[0] = limits.maxViewportDimensions[1] = 4096;
		limits.viewportBoundsRange[0] = -8192.0f;
		limits.viewportBoundsRange[1] = 8192.0f;
		Pool->DeviceState = State;
		Pool->FamilyIndex = 0;
		State->Dispatch.vkBeginCommandBuffer = +[](VkCommandBuffer, const VkCommandBufferBeginInfo*) -> VkResult { return VK_SUCCESS; };
		State->Dispatch.vkEndCommandBuffer = +[](VkCommandBuffer) -> VkResult { return VK_SUCCESS; };
		State->Dispatch.vkFreeCommandBuffers = +[](VkDevice, VkCommandPool, std::uint32_t, const VkCommandBuffer*) {};
		State->Dispatch.vkCmdPipelineBarrier2 = +[](VkCommandBuffer, const VkDependencyInfo* info)
		{
			for (std::uint32_t i = 0; i < info->imageMemoryBarrierCount; ++i)
			{
				capture->Images.push_back(info->pImageMemoryBarriers[i]);
			}
			for (std::uint32_t i = 0; i < info->bufferMemoryBarrierCount; ++i)
			{
				capture->Buffers.push_back(info->pBufferMemoryBarriers[i]);
			}
		};
		State->Dispatch.vkCmdBeginRendering = +[](VkCommandBuffer, const VkRenderingInfo* info)
		{
			++capture->BeginCount;
			capture->Colors.clear();
			for (std::uint32_t i = 0; i < info->colorAttachmentCount; ++i)
			{
				capture->Colors.push_back(info->pColorAttachments[i]);
			}
			capture->HasDepth = info->pDepthAttachment != nullptr;
			capture->HasStencil = info->pStencilAttachment != nullptr;
			if (capture->HasDepth)
			{
				capture->Depth = *info->pDepthAttachment;
			}
		};
		State->Dispatch.vkCmdEndRendering = +[](VkCommandBuffer) { ++capture->EndCount; };
		State->Dispatch.vkCmdCopyBuffer = +[](VkCommandBuffer, VkBuffer, VkBuffer, std::uint32_t, const VkBufferCopy* copy)
		{
			capture->BufferCopy = *copy;
			++capture->CopyCount;
		};
		State->Dispatch.vkCmdCopyImage = +[](VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, std::uint32_t, const VkImageCopy* copy)
		{
			capture->ImageCopy = *copy;
			++capture->CopyCount;
		};
		State->Dispatch.vkCmdCopyBufferToImage = +[](VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, std::uint32_t, const VkBufferImageCopy* copy)
		{
			capture->BufferImageCopy = *copy;
			++capture->CopyCount;
		};
		State->Dispatch.vkCmdCopyImageToBuffer = +[](VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, std::uint32_t, const VkBufferImageCopy* copy)
		{
			capture->BufferImageCopy = *copy;
			++capture->CopyCount;
		};
		State->Dispatch.vkCmdSetViewport = +[](VkCommandBuffer, std::uint32_t, std::uint32_t, const VkViewport* viewport)
		{
			capture->Viewport = *viewport;
		};
		State->Dispatch.vkCmdSetScissor = +[](VkCommandBuffer, std::uint32_t, std::uint32_t, const VkRect2D* scissor)
		{
			capture->Scissor = *scissor;
		};
		State->Dispatch.vkQueueSubmit2 = +[](VkQueue, std::uint32_t, const VkSubmitInfo2*, VkFence) -> VkResult
		{
			++capture->SubmitCount;
			return capture->SubmitResult;
		};
		Commands = std::make_unique<RhiVulkan::VulkanCommandList>(Pool, RhiVulkan::FromNativeHandle<VkCommandBuffer>(1));
	}

	VulkanCommandCapture::~VulkanCommandCapture()
	{
		Commands.reset();
		capture = nullptr;
	}

} // namespace Swim::Testing
