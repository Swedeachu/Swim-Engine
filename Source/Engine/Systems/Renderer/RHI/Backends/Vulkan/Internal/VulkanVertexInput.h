#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>
#include <vector>

namespace Swim::RhiVulkan
{

	struct VulkanDeviceState;

	struct VulkanVertexBindingRequirement
	{
		std::uint32_t Slot = 0;
		std::uint32_t Stride = 0;
		Rhi::VertexInputRate Rate = Rhi::VertexInputRate::Vertex;
		std::uint64_t ElementBytes = 0;
		std::uint32_t Alignment = 1;
	};

	struct VulkanVertexInputLayout
	{
		std::vector<VkVertexInputBindingDescription> Bindings;
		std::vector<VkVertexInputAttributeDescription> Attributes;
		std::vector<VulkanVertexBindingRequirement> Requirements;
	};

	// Invalid layouts throw before native pipeline creation. Used attributes
	// must be naturally aligned and fit nonzero strides; zero stride is constant.
	VulkanVertexInputLayout BuildVulkanVertexInput(const VulkanDeviceState& state,
		std::span<const Rhi::VertexBindingDesc> bindings, std::span<const Rhi::VertexAttributeDesc> attributes);

} // namespace Swim::RhiVulkan
