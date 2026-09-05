#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorTable.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanGraphicsPipeline.h"

namespace Swim::RhiVulkan
{

	void VulkanCommandList::BindDescriptorTable(std::uint32_t space, Rhi::DescriptorTable& table)
	{
		RequireRecording();
		RequireGraphicsQueue();
		if (graphicsPipeline == nullptr)
		{
			throw std::logic_error("Bind a graphics pipeline before its descriptor tables");
		}
		auto& native = RequireResource<VulkanDescriptorTable>(table, GetState());
		const auto& layout = graphicsPipeline->GetLayoutState();
		if (native.GetLayoutState() != layout || space != native.GetSpace() || space >= boundTables.size() || !native.IsComplete())
		{
			throw std::invalid_argument("Descriptor table must match the pipeline layout/space and have every element initialized");
		}
		const auto set = FromNativeHandle<VkDescriptorSet>(native.GetNativeHandle());
		GetState()->Dispatch.vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->Layout, space, 1, &set, 0, nullptr);
		native.Seal();
		boundTables[space] = &native;
	}

	void VulkanCommandList::RequireDescriptorTables() const
	{
		for (const auto& schema : graphicsPipeline->GetLayoutState()->Interface.DescriptorSchemas)
		{
			if (!schema.Bindings.empty() && (schema.Space >= boundTables.size() || boundTables[schema.Space] == nullptr))
			{
				throw std::logic_error("Every reflected descriptor space must be bound before drawing");
			}
		}
	}

} // namespace Swim::RhiVulkan
