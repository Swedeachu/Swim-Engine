#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"

namespace Swim::RhiVulkan
{

	VulkanCommandList::~VulkanCommandList()
	{
		if (commandBuffer != VK_NULL_HANDLE)
		{
			poolState->DeviceState->Dispatch.vkFreeCommandBuffers(
				poolState->DeviceState->Device.device, poolState->Pool, 1, &commandBuffer);
		}
	}

	std::uintptr_t VulkanCommandList::GetNativeHandle() const
	{
		return ToNativeHandle(commandBuffer);
	}

	void VulkanCommandList::RequireRecording(bool outsideRendering) const
	{
		if (!recording || generation != poolState->Generation || (outsideRendering && rendering))
		{
			throw std::logic_error("Vulkan command requires recording in the appropriate rendering scope");
		}
	}

	void VulkanCommandList::RequireGraphicsQueue() const
	{
		if (poolState->FamilyIndex != GetState()->QueueFamilies.Graphics)
		{
			throw std::logic_error("This Vulkan command path requires the graphics queue family");
		}
	}

	void VulkanCommandList::Begin()
	{
		if (generation == poolState->Generation)
		{
			throw std::logic_error("Vulkan one-time command list requires a pool reset before recording again");
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (poolState->DeviceState->Dispatch.vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to begin Vulkan command buffer");
		}
		generation = poolState->Generation;
		debugLabelDepth = 0;
		executable = false;
		rendering = false;
		recording = true;
		graphicsPipeline = nullptr;
		boundTables.clear();
		availableIndices = 0;
		viewportSet = false;
		scissorSet = false;
		renderingColors.clear();
		renderingDepth = Rhi::Format::Undefined;
	}

	void VulkanCommandList::End()
	{
		RequireRecording(true);
		if (debugLabelDepth != 0)
		{
			throw std::logic_error("Close all RHI debug label regions before ending a command list");
		}
		if (poolState->DeviceState->Dispatch.vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to end Vulkan command buffer");
		}
		recording = false;
		executable = true;
	}

	void VulkanCommandList::BindComputePipeline(Rhi::ComputePipeline&)
	{
		throw std::logic_error("Vulkan compute pipelines are implemented with item 39");
	}

	void VulkanCommandList::BindVertexBuffer(std::uint32_t, Rhi::Buffer&, std::uint64_t)
	{
		throw std::logic_error("Vulkan vertex binding is implemented with item 39");
	}

	void VulkanCommandList::Dispatch(std::uint32_t, std::uint32_t, std::uint32_t)
	{
		throw std::logic_error("Vulkan dispatch commands are implemented with item 39");
	}

	void VulkanCommandList::WriteTimestamp(Rhi::QueryPool&, std::uint32_t)
	{
		throw std::logic_error("Vulkan timestamps are implemented with item 39");
	}

} // namespace Swim::RhiVulkan
