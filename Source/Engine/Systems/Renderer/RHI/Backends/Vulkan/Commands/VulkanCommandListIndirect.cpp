#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"

namespace Swim::RhiVulkan
{

	void VulkanCommandList::RequireIndirectDraw(
		const Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) const
	{
		RequireDraw();
		if (availableIndices == 0)
		{
			throw std::logic_error("Vulkan indexed indirect draw requires a bound index buffer");
		}
		// Index counts and vertex ranges come from GPU memory: only binding presence is validated.
		RequireVertexBuffers(true, 0, 0, 0, 0);
		const auto& desc = arguments.GetDesc();
		constexpr std::uint64_t record = sizeof(Rhi::DrawIndexedIndirectCommand);
		if (!HasBufferUsage(desc.Usage, Rhi::BufferUsage::Indirect) || offset % 4 != 0 || stride < record || stride % 4 != 0)
		{
			throw std::invalid_argument(
				"Indirect draw arguments need Indirect usage, a four-byte aligned offset and a stride of at least 20 bytes");
		}
		const auto& limits = GetState()->Device.physical_device.properties.limits;
		if (drawCount > limits.maxDrawIndirectCount ||
			(drawCount > 0 &&
				(offset > desc.Size || (desc.Size - offset < record) || (drawCount - 1) > (desc.Size - offset - record) / stride)))
		{
			throw std::invalid_argument("Indirect draw arguments exceed the buffer or the device draw-count limit");
		}
	}

	void VulkanCommandList::DrawIndexedIndirect(Rhi::Buffer& arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
	{
		const auto& native = RequireResource<VulkanBuffer>(arguments, GetState());
		RequireIndirectDraw(native, offset, drawCount, stride);
		if (drawCount == 0)
		{
			return; // Nothing to record.
		}
		GetState()->Dispatch.vkCmdDrawIndexedIndirect(
			commandBuffer, FromNativeHandle<VkBuffer>(native.GetNativeHandle()), offset, drawCount, stride);
	}

	void VulkanCommandList::DrawIndexedIndirectCount(Rhi::Buffer& arguments, std::uint64_t offset, Rhi::Buffer& count,
		std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride)
	{
		const auto& native = RequireResource<VulkanBuffer>(arguments, GetState());
		const auto& counter = RequireResource<VulkanBuffer>(count, GetState());
		RequireIndirectDraw(native, offset, maxDrawCount, stride);
		const auto& countDesc = counter.GetDesc();
		if (!HasBufferUsage(countDesc.Usage, Rhi::BufferUsage::Indirect) || countOffset % 4 != 0 || countOffset > countDesc.Size ||
			countDesc.Size - countOffset < sizeof(std::uint32_t))
		{
			throw std::invalid_argument("Indirect draw count needs Indirect usage and an in-range four-byte aligned uint32");
		}
		GetState()->Dispatch.vkCmdDrawIndexedIndirectCount(commandBuffer, FromNativeHandle<VkBuffer>(native.GetNativeHandle()), offset,
			FromNativeHandle<VkBuffer>(counter.GetNativeHandle()), countOffset, maxDrawCount, stride);
	}

} // namespace Swim::RhiVulkan
