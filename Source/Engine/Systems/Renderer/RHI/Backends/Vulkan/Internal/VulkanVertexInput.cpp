#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanVertexInput.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	namespace
	{
		std::uint32_t AttributeAlignment(Rhi::Format format)
		{
			using Rhi::Format;
			switch (format)
			{
			case Format::R8Unorm: case Format::R8Snorm: case Format::R8Uint: case Format::R8Sint:
			case Format::RG8Unorm: case Format::RG8Snorm: case Format::RG8Uint: case Format::RG8Sint:
			case Format::RGBA8Unorm: case Format::RGBA8Snorm: case Format::RGBA8Uint: case Format::RGBA8Sint:
			case Format::BGRA8Unorm:
				return 1;
			case Format::R16Unorm: case Format::R16Snorm: case Format::R16Uint: case Format::R16Sint: case Format::R16Float:
			case Format::RG16Unorm: case Format::RG16Snorm: case Format::RG16Uint: case Format::RG16Sint: case Format::RG16Float:
			case Format::RGBA16Unorm: case Format::RGBA16Snorm: case Format::RGBA16Uint: case Format::RGBA16Sint: case Format::RGBA16Float:
				return 2;
			case Format::R32Uint: case Format::R32Sint: case Format::R32Float:
			case Format::RG32Uint: case Format::RG32Sint: case Format::RG32Float:
			case Format::RGB32Uint: case Format::RGB32Sint: case Format::RGB32Float:
			case Format::RGBA32Uint: case Format::RGBA32Sint: case Format::RGBA32Float:
			case Format::RGB10A2Unorm: case Format::RGB10A2Uint: case Format::BGR10A2Unorm:
			case Format::R11G11B10Float: case Format::RGB9E5Float:
				return 4;
			default:
				throw std::invalid_argument("Vertex attributes require a supported uncompressed linear color format");
			}
		}
	}

	VulkanVertexInputLayout BuildVulkanVertexInput(const VulkanDeviceState& state,
		std::span<const Rhi::VertexBindingDesc> bindings, std::span<const Rhi::VertexAttributeDesc> attributes)
	{
		RequireVulkanDevice(state);
		const auto& limits = state.Device.physical_device.properties.limits;
		if (bindings.size() > limits.maxVertexInputBindings || attributes.size() > limits.maxVertexInputAttributes)
		{
			throw std::invalid_argument("Vertex layout exceeds device binding/attribute limits");
		}
		VulkanVertexInputLayout result;
		for (const auto& binding : bindings)
		{
			if (binding.Slot >= limits.maxVertexInputBindings || binding.Stride > limits.maxVertexInputBindingStride ||
				(binding.Rate != Rhi::VertexInputRate::Vertex && binding.Rate != Rhi::VertexInputRate::Instance) ||
				std::any_of(result.Bindings.begin(), result.Bindings.end(), [&](const auto& other) { return other.binding == binding.Slot; }))
			{
				throw std::invalid_argument("Invalid or duplicate vertex binding");
			}
			result.Bindings.push_back({ binding.Slot, binding.Stride,
				binding.Rate == Rhi::VertexInputRate::Vertex ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE });
		}
		for (const auto& attribute : attributes)
		{
			const auto binding = std::find_if(bindings.begin(), bindings.end(),
				[&](const auto& item) { return item.Slot == attribute.Slot; });
			if (binding == bindings.end() || attribute.Location >= limits.maxVertexInputAttributes ||
				attribute.Offset > limits.maxVertexInputAttributeOffset ||
				std::any_of(result.Attributes.begin(), result.Attributes.end(), [&](const auto& other) { return other.location == attribute.Location; }))
			{
				throw std::invalid_argument("Vertex attribute has an invalid/duplicate location, offset or missing binding");
			}
			const auto alignment = AttributeAlignment(attribute.DataFormat);
			const auto bytes = GetColorTexelBytes(attribute.DataFormat);
			const auto end = static_cast<std::uint64_t>(attribute.Offset) + bytes;
			if (attribute.Offset % alignment != 0 || binding->Stride % alignment != 0 ||
				(binding->Stride != 0 && end > binding->Stride))
			{
				throw std::invalid_argument("Vertex attributes must be aligned and fit the binding stride");
			}
			const auto format = ToVkFormat(attribute.DataFormat);
			VkFormatProperties properties{};
			state.Instance->Dispatch.vkGetPhysicalDeviceFormatProperties(state.Device.physical_device.physical_device, format, &properties);
			if ((properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0)
			{
				throw std::invalid_argument("Device does not support this vertex attribute format");
			}
			result.Attributes.push_back({ attribute.Location, attribute.Slot, format, attribute.Offset });
			auto requirement = std::find_if(result.Requirements.begin(), result.Requirements.end(),
				[&](const auto& item) { return item.Slot == attribute.Slot; });
			if (requirement == result.Requirements.end())
			{
				result.Requirements.push_back({ binding->Slot, binding->Stride, binding->Rate, end, alignment });
			}
			else
			{
				requirement->ElementBytes = std::max(requirement->ElementBytes, end);
				requirement->Alignment = std::max(requirement->Alignment, alignment);
			}
		}
		return result;
	}

} // namespace Swim::RhiVulkan
