#pragma once

#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

#include <volk.h>

namespace Swim::RhiVulkan
{

	VkCompareOp ToVkCompareOp(Rhi::CompareOp value);
	VkStencilOp ToVkStencilOp(Rhi::StencilOp value);
	VkBlendFactor ToVkBlendFactor(Rhi::BlendFactor value);
	VkBlendOp ToVkBlendOp(Rhi::BlendOp value);
	VkPrimitiveTopology ToVkPrimitiveTopology(Rhi::PrimitiveTopology value);
	VkCullModeFlags ToVkCullMode(Rhi::CullMode value);
	VkFrontFace ToVkFrontFace(Rhi::FrontFace value);

} // namespace Swim::RhiVulkan
