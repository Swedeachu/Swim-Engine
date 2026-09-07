#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanGraphicsPipeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"

#include <algorithm>

namespace Swim::RhiVulkan
{

	void VulkanCommandList::BindVertexBuffer(std::uint32_t slot, Rhi::Buffer& buffer, std::uint64_t offset)
	{
		RequireRecording();
		RequireGraphicsQueue();
		RequireResource<VulkanBuffer>(buffer, GetState());
		if (slot >= GetState()->Device.physical_device.properties.limits.maxVertexInputBindings ||
			!HasBufferUsage(buffer.GetDesc().Usage, Rhi::BufferUsage::Vertex) ||
			offset >= buffer.GetDesc().Size || buffer.GetNativeHandle() == 0)
		{
			throw std::invalid_argument("Vertex binding requires a valid slot, vertex buffer and in-range offset");
		}
		const VertexBufferBinding replacement{ slot, offset, buffer.GetDesc().Size - offset };
		auto existing = std::find_if(vertexBuffers.begin(), vertexBuffers.end(), [&](const auto& item) { return item.Slot == slot; });
		if (existing == vertexBuffers.end())
		{
			// Allocate bookkeeping before recording the non-failing native command.
			vertexBuffers.push_back(replacement);
		}
		else
		{
			*existing = replacement;
		}
		const auto native = FromNativeHandle<VkBuffer>(buffer.GetNativeHandle());
		const VkDeviceSize nativeOffset = offset;
		GetState()->Dispatch.vkCmdBindVertexBuffers(commandBuffer, slot, 1, &native, &nativeOffset);
	}

	void VulkanCommandList::RequireVertexBuffers(bool indexed, std::uint32_t elementCount, std::uint32_t instanceCount,
		std::uint32_t firstVertex, std::uint32_t firstInstance) const
	{
		for (const auto& requirement : graphicsPipeline->GetVertexRequirements())
		{
			const auto binding = std::find_if(vertexBuffers.begin(), vertexBuffers.end(),
				[&](const auto& item) { return item.Slot == requirement.Slot; });
			if (binding == vertexBuffers.end() || binding->Offset % requirement.Alignment != 0 ||
				binding->Bytes < requirement.ElementBytes)
			{
				throw std::invalid_argument("Draw requires every vertex attribute binding with aligned, sufficient storage");
			}
			const bool perInstance = requirement.Rate == Rhi::VertexInputRate::Instance;
			if (elementCount == 0 || instanceCount == 0 || (indexed && !perInstance) || requirement.Stride == 0)
			{
				// Index contents and signed base-vertex effects remain caller-owned.
				// Do not map/read back index buffers merely to validate a draw.
				continue;
			}
			const std::uint64_t first = perInstance ? firstInstance : firstVertex;
			const std::uint64_t count = perInstance ? instanceCount : elementCount;
			const auto last = first + count - 1;
			// Division avoids overflow even for 64-bit buffers and extreme draw arguments.
			if (last > (binding->Bytes - requirement.ElementBytes) / requirement.Stride)
			{
				throw std::invalid_argument("Draw exceeds the bound vertex or instance buffer");
			}
		}
	}

} // namespace Swim::RhiVulkan
