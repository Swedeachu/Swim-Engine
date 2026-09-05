#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanGraphicsPipeline.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"

namespace Swim::RhiVulkan
{

	void VulkanCommandList::BindGraphicsPipeline(Rhi::GraphicsPipeline& pipeline)
	{
		RequireRecording();
		RequireGraphicsQueue();
		auto& native = RequireResource<VulkanGraphicsPipeline>(pipeline, GetState());
		boundTables.assign(native.GetLayoutState()->Sets.size(), nullptr);
		GetState()->Dispatch.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			FromNativeHandle<VkPipeline>(native.GetNativeHandle()));
		graphicsPipeline = &native;
	}

	void VulkanCommandList::BindIndexBuffer(Rhi::Buffer& buffer, std::uint64_t offset, Rhi::IndexType type)
	{
		RequireRecording();
		RequireGraphicsQueue();
		RequireResource<VulkanBuffer>(buffer, GetState());
		if (type != Rhi::IndexType::Uint16 && type != Rhi::IndexType::Uint32)
		{
			throw std::invalid_argument("Unsupported Vulkan index type");
		}
		const std::uint32_t stride = type == Rhi::IndexType::Uint16 ? 2 : 4;
		if (!HasBufferUsage(buffer.GetDesc().Usage, Rhi::BufferUsage::Index) || offset >= buffer.GetDesc().Size || offset % stride != 0)
		{
			throw std::invalid_argument("Vulkan index binding requires index usage and an aligned in-range offset");
		}
		GetState()->Dispatch.vkCmdBindIndexBuffer(commandBuffer, FromNativeHandle<VkBuffer>(buffer.GetNativeHandle()), offset,
			type == Rhi::IndexType::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
		availableIndices = (buffer.GetDesc().Size - offset) / stride;
	}

	void VulkanCommandList::RequireDraw() const
	{
		RequireRecording();
		RequireGraphicsQueue();
		if (!rendering || graphicsPipeline == nullptr || !viewportSet || !scissorSet)
		{
			throw std::logic_error("Vulkan draw requires rendering, a graphics pipeline, viewport and scissor");
		}
		RequireDescriptorTables();
		if (!graphicsPipeline->MatchesRendering(renderingColors, renderingDepth, renderingSamples))
		{
			throw std::invalid_argument("Vulkan graphics pipeline formats and samples must match the active attachments");
		}
	}

	void VulkanCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
	{
		RequireDraw();
		GetState()->Dispatch.vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void VulkanCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
		std::int32_t vertexOffset, std::uint32_t firstInstance)
	{
		RequireDraw();
		if (availableIndices == 0 || firstIndex > availableIndices || indexCount > availableIndices - firstIndex)
		{
			throw std::invalid_argument("Vulkan indexed draw exceeds the bound index buffer");
		}
		GetState()->Dispatch.vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

} // namespace Swim::RhiVulkan
