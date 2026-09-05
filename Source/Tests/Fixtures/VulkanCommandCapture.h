#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"

#include <vector>

namespace Swim::Testing
{

	// Dispatch capture owns no native objects and never invokes a Vulkan loader.
	struct VulkanCommandCapture
	{
		VulkanCommandCapture();
		~VulkanCommandCapture();

		std::shared_ptr<RhiVulkan::VulkanDeviceState> State;
		std::shared_ptr<RhiVulkan::VulkanCommandPoolState> Pool;
		std::unique_ptr<RhiVulkan::VulkanCommandList> Commands;
		std::vector<VkImageMemoryBarrier2> Images;
		std::vector<VkBufferMemoryBarrier2> Buffers;
		std::vector<VkRenderingAttachmentInfo> Colors;
		VkRenderingAttachmentInfo Depth{};
		VkBufferCopy BufferCopy{};
		VkImageCopy ImageCopy{};
		VkBufferImageCopy BufferImageCopy{};
		VkViewport Viewport{};
		VkRect2D Scissor{};
		bool HasDepth = false;
		bool HasStencil = false;
		std::uint32_t BeginCount = 0;
		std::uint32_t EndCount = 0;
		std::uint32_t CopyCount = 0;
		std::uint32_t SubmitCount = 0;
		VkResult SubmitResult = VK_SUCCESS;
	};

} // namespace Swim::Testing
