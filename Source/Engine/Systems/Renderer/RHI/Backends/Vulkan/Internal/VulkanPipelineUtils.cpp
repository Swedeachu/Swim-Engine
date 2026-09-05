#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineUtils.h"

#include <stdexcept>

namespace Swim::RhiVulkan
{

	VkCompareOp ToVkCompareOp(Rhi::CompareOp value)
	{
		switch (value)
		{
		case Rhi::CompareOp::Never: return VK_COMPARE_OP_NEVER;
		case Rhi::CompareOp::Less: return VK_COMPARE_OP_LESS;
		case Rhi::CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
		case Rhi::CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case Rhi::CompareOp::Greater: return VK_COMPARE_OP_GREATER;
		case Rhi::CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
		case Rhi::CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case Rhi::CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
		default: throw std::invalid_argument("Unknown RHI CompareOp");
		}
	}

	VkStencilOp ToVkStencilOp(Rhi::StencilOp value)
	{
		switch (value)
		{
		case Rhi::StencilOp::Keep: return VK_STENCIL_OP_KEEP;
		case Rhi::StencilOp::Zero: return VK_STENCIL_OP_ZERO;
		case Rhi::StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
		case Rhi::StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case Rhi::StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case Rhi::StencilOp::Invert: return VK_STENCIL_OP_INVERT;
		case Rhi::StencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case Rhi::StencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default: throw std::invalid_argument("Unknown RHI StencilOp");
		}
	}

	VkBlendFactor ToVkBlendFactor(Rhi::BlendFactor value)
	{
		switch (value)
		{
		case Rhi::BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
		case Rhi::BlendFactor::One: return VK_BLEND_FACTOR_ONE;
		case Rhi::BlendFactor::SourceColor: return VK_BLEND_FACTOR_SRC_COLOR;
		case Rhi::BlendFactor::OneMinusSourceColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case Rhi::BlendFactor::DestinationColor: return VK_BLEND_FACTOR_DST_COLOR;
		case Rhi::BlendFactor::OneMinusDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case Rhi::BlendFactor::SourceAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
		case Rhi::BlendFactor::OneMinusSourceAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case Rhi::BlendFactor::DestinationAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
		case Rhi::BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		default: throw std::invalid_argument("Unknown RHI BlendFactor");
		}
	}

	VkBlendOp ToVkBlendOp(Rhi::BlendOp value)
	{
		switch (value)
		{
		case Rhi::BlendOp::Add: return VK_BLEND_OP_ADD;
		case Rhi::BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
		case Rhi::BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
		case Rhi::BlendOp::Min: return VK_BLEND_OP_MIN;
		case Rhi::BlendOp::Max: return VK_BLEND_OP_MAX;
		default: throw std::invalid_argument("Unknown RHI BlendOp");
		}
	}

	VkPrimitiveTopology ToVkPrimitiveTopology(Rhi::PrimitiveTopology value)
	{
		switch (value)
		{
		case Rhi::PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case Rhi::PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case Rhi::PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case Rhi::PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case Rhi::PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		default: throw std::invalid_argument("Unknown RHI PrimitiveTopology");
		}
	}

	VkCullModeFlags ToVkCullMode(Rhi::CullMode value)
	{
		switch (value)
		{
		case Rhi::CullMode::None: return VK_CULL_MODE_NONE;
		case Rhi::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
		case Rhi::CullMode::Back: return VK_CULL_MODE_BACK_BIT;
		default: throw std::invalid_argument("Unknown RHI CullMode");
		}
	}

	VkFrontFace ToVkFrontFace(Rhi::FrontFace value)
	{
		switch (value)
		{
		case Rhi::FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		case Rhi::FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
		default: throw std::invalid_argument("Unknown RHI FrontFace");
		}
	}

} // namespace Swim::RhiVulkan
