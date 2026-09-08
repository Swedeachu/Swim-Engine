#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"

namespace Swim::RhiVulkan
{

	VulkanCommandList::~VulkanCommandList()
	{
		RetireLostVulkanDevice(*poolState->DeviceState);
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
		RequireVulkanDevice(*GetState());
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
		RequireVulkanDevice(*GetState());
		if (generation == poolState->Generation)
		{
			throw std::logic_error("Vulkan one-time command list requires a pool reset before recording again");
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (CheckVulkanResult(*poolState->DeviceState, poolState->DeviceState->Dispatch.vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer") != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to begin Vulkan command buffer");
		}
		generation = poolState->Generation;
		debugLabelDepth = 0;
		executable = false;
		rendering = false;
		recording = true;
		graphicsPipeline = nullptr;
		computePipeline = nullptr;
		boundTables.clear();
		pushConstantRanges.clear();
		initializedPushConstants.clear();
		availableIndices = 0;
		vertexBuffers.clear();
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
		if (CheckVulkanResult(*poolState->DeviceState, poolState->DeviceState->Dispatch.vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer") != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to end Vulkan command buffer");
		}
		recording = false;
		executable = true;
	}

} // namespace Swim::RhiVulkan
