#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPushConstants.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	namespace
	{
		VkShaderStageFlags SupportedStages(Rhi::ShaderStageMask stages)
		{
			const auto mask = static_cast<std::uint32_t>(stages);
			const auto allowed = static_cast<std::uint32_t>(Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment | Rhi::ShaderStageMask::Compute);
			if (mask == 0 || (mask & ~allowed) != 0)
			{
				throw std::invalid_argument("Push constants require explicit vertex/fragment/compute visibility");
			}
			return ((mask & static_cast<std::uint32_t>(Rhi::ShaderStageMask::Vertex)) ? VK_SHADER_STAGE_VERTEX_BIT : 0) |
				((mask & static_cast<std::uint32_t>(Rhi::ShaderStageMask::Fragment)) ? VK_SHADER_STAGE_FRAGMENT_BIT : 0) |
				((mask & static_cast<std::uint32_t>(Rhi::ShaderStageMask::Compute)) ? VK_SHADER_STAGE_COMPUTE_BIT : 0);
		}
	}

	std::vector<VkPushConstantRange> BuildVulkanPushConstantRanges(const VulkanDeviceState& state,
		std::span<const Rhi::PushConstantRange> ranges, VkShaderStageFlags programStages)
	{
		RequireVulkanDevice(state);
		const auto limit = state.Device.physical_device.properties.limits.maxPushConstantsSize;
		VkShaderStageFlags seen = 0;
		std::vector<VkPushConstantRange> result;
		for (const auto& range : ranges)
		{
			const auto stages = SupportedStages(range.Stages);
			if ((stages & ~programStages) != 0 || (seen & stages) != 0 || range.Size == 0 ||
				range.Offset % 4 != 0 || range.Size % 4 != 0 || range.Offset >= limit || range.Size > limit - range.Offset)
			{
				throw std::invalid_argument("Invalid push-constant range, repeated stage or device limit exceeded");
			}
			seen |= stages;
			result.push_back({ stages, range.Offset, range.Size });
		}
		// Stable ordering makes identical range sets compatible across layout objects.
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.stageFlags < right.stageFlags; });
		return result;
	}

	bool CompatibleVulkanPushConstants(std::span<const VkPushConstantRange> left, std::span<const VkPushConstantRange> right)
	{
		return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b)
		{
			return a.stageFlags == b.stageFlags && a.offset == b.offset && a.size == b.size;
		});
	}

	VkShaderStageFlags ValidateVulkanPushConstantWrite(std::span<const VkPushConstantRange> ranges,
		Rhi::ShaderStageMask stages, std::uint32_t offset, std::size_t size)
	{
		const auto flags = SupportedStages(stages);
		if (size == 0 || offset % 4 != 0 || size % 4 != 0 || size > UINT32_MAX - offset)
		{
			throw std::invalid_argument("Push-constant writes require a nonempty aligned byte range");
		}
		const auto end = offset + static_cast<std::uint32_t>(size);
		VkShaderStageFlags covered = 0;
		for (const auto& range : ranges)
		{
			const auto rangeEnd = range.offset + range.size;
			if (offset < rangeEnd && end > range.offset && (range.stageFlags & ~flags) != 0)
			{
				throw std::invalid_argument("Push-constant write omits stages from an overlapping range");
			}
			if (offset >= range.offset && end <= rangeEnd)
			{
				covered |= range.stageFlags;
			}
		}
		if ((flags & ~covered) != 0)
		{
			throw std::invalid_argument("Push-constant write is not covered by every requested stage");
		}
		return flags;
	}

} // namespace Swim::RhiVulkan
