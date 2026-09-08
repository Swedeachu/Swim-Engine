#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPushConstants.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorLayout.h"

#include <algorithm>

namespace Swim::RhiVulkan
{

	void VulkanCommandList::PushConstants(Rhi::ShaderStageMask stages, std::uint32_t offset, std::span<const std::byte> data)
	{
		RequireRecording();
		const auto& layout = RequireActivePipeline();
		const auto flags = ValidateVulkanPushConstantWrite(layout.PushConstants, stages, offset, data.size());
		if (data.data() == nullptr)
		{
			throw std::invalid_argument("Push-constant data is null");
		}
		if (!CompatibleVulkanPushConstants(pushConstantRanges, layout.PushConstants))
		{
			std::uint32_t words = 0;
			for (const auto& range : layout.PushConstants)
			{
				words = std::max(words, (range.offset + range.size) / 4);
			}
			// Allocate before changing state or recording the non-failing native command.
			auto ranges = layout.PushConstants;
			std::vector<VkShaderStageFlags> initialized(words, 0);
			initializedPushConstants = std::move(initialized);
			pushConstantRanges = std::move(ranges);
		}
		GetState()->Dispatch.vkCmdPushConstants(commandBuffer, layout.Layout, flags, offset,
			static_cast<std::uint32_t>(data.size()), data.data());
		for (std::size_t word = offset / 4; word < (offset + data.size()) / 4; ++word)
		{
			initializedPushConstants[word] |= flags;
		}
	}

	void VulkanCommandList::RequirePushConstants() const
	{
		const auto& ranges = RequireActivePipeline().PushConstants;
		if (ranges.empty())
		{
			return;
		}
		if (!CompatibleVulkanPushConstants(pushConstantRanges, ranges))
		{
			throw std::logic_error("Draw/dispatch requires push constants written with a compatible layout");
		}
		for (const auto& range : ranges)
		{
			for (std::uint32_t word = range.offset / 4; word < (range.offset + range.size) / 4; ++word)
			{
				if ((initializedPushConstants[word] & range.stageFlags) != range.stageFlags)
				{
					throw std::logic_error("Draw/dispatch requires every reflected push-constant byte initialized for its stages");
				}
			}
		}
	}

} // namespace Swim::RhiVulkan
