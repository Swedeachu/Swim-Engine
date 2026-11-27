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

		if (space != native.GetSpace() || space >= layout.Sets.size() || !native.IsComplete())
		{
			throw std::invalid_argument(
				"Descriptor table must match an identically defined pipeline layout space and have every required element initialized");
		}

		(computePipeline ? computeTables : graphicsTables).Bind(space, &native, native.GetLayoutState()->Interface, layout.Interface);

		const auto set = FromNativeHandle<VkDescriptorSet>(native.GetNativeHandle());
		GetState()->Dispatch.vkCmdBindDescriptorSets(commandBuffer, computePipeline ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, layout.Layout, space, 1, &set, 0, nullptr);
		native.Seal();
	}

	void VulkanCommandList::RequireDescriptorTables() const
	{
		(computePipeline ? computeTables : graphicsTables).Require(RequireActivePipeline().Interface);
	}

} // namespace Swim::RhiVulkan
