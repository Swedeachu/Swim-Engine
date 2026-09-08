#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorTable.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorLayout.h"

namespace Swim::RhiVulkan
{

	void VulkanCommandList::BindDescriptorTable(std::uint32_t space, Rhi::DescriptorTable& table)
	{
		RequireRecording();
		const auto& layout = RequireActivePipeline();
		auto& native = RequireResource<VulkanDescriptorTable>(table, GetState());
		if (native.GetLayoutState().get() != &layout || space != native.GetSpace() || space >= boundTables.size() || !native.IsComplete())
		{
			throw std::invalid_argument("Descriptor table must match the pipeline layout/space and have every element initialized");
		}
		const auto set = FromNativeHandle<VkDescriptorSet>(native.GetNativeHandle());
		GetState()->Dispatch.vkCmdBindDescriptorSets(commandBuffer, computePipeline ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, layout.Layout, space, 1, &set, 0, nullptr);
		native.Seal();
		boundTables[space] = &native;
	}

	void VulkanCommandList::RequireDescriptorTables() const
	{
		for (const auto& schema : RequireActivePipeline().Interface.DescriptorSchemas)
		{
			if (!schema.Bindings.empty() && (schema.Space >= boundTables.size() || boundTables[schema.Space] == nullptr))
			{
				throw std::logic_error("Every reflected descriptor space must be bound before drawing or dispatching");
			}
		}
	}

} // namespace Swim::RhiVulkan
