#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorLayout.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	VulkanPipelineLayoutState::~VulkanPipelineLayoutState()
	{
		if (Layout != VK_NULL_HANDLE)
		{
			Device->Dispatch.vkDestroyPipelineLayout(Device->Device.device, Layout, nullptr);
		}
		for (const auto set : Sets)
		{
			if (set != VK_NULL_HANDLE)
			{
				Device->Dispatch.vkDestroyDescriptorSetLayout(Device->Device.device, set, nullptr);
			}
		}
	}

	VkDescriptorType ToVkDescriptorType(Rhi::DescriptorType type)
	{
		switch (type)
		{
		case Rhi::DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
		case Rhi::DescriptorType::SampledTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case Rhi::DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case Rhi::DescriptorType::ReadOnlyStorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		default: throw std::invalid_argument("Descriptor type is not supported by the graphics resource baseline");
		}
	}

	VkShaderStageFlags ToVkDescriptorStages(Rhi::ShaderStageMask stages)
	{
		const auto mask = static_cast<std::uint32_t>(stages);
		if (mask == 0 || (mask & ~3u) != 0)
		{
			throw std::invalid_argument("Graphics descriptors require explicit vertex/fragment visibility");
		}
		return ((mask & 1u) ? VK_SHADER_STAGE_VERTEX_BIT : 0) | ((mask & 2u) ? VK_SHADER_STAGE_FRAGMENT_BIT : 0);
	}

	const Rhi::DescriptorSchemaDesc* FindDescriptorSchema(const VulkanPipelineLayoutState& layout, std::uint32_t space)
	{
		for (const auto& schema : layout.Interface.DescriptorSchemas)
		{
			if (schema.Space == space)
			{
				return &schema;
			}
		}
		return nullptr;
	}

	bool CreateDescriptorLayouts(VulkanPipelineLayoutState& layout)
	{
		const auto& limits = layout.Device->Device.physical_device.properties.limits;
		auto& schemas = layout.Interface.DescriptorSchemas;
		std::sort(schemas.begin(), schemas.end(), [](const auto& a, const auto& b) { return a.Space < b.Space; });
		std::array<std::uint64_t, 4> totals{};
		std::array<std::array<std::uint64_t, 4>, 2> perStage{};
		const std::array<std::uint32_t, 4> totalLimits{ limits.maxDescriptorSetSamplers, limits.maxDescriptorSetSampledImages,
			limits.maxDescriptorSetUniformBuffers, limits.maxDescriptorSetStorageBuffers };
		const std::array<std::uint32_t, 4> stageLimits{ limits.maxPerStageDescriptorSamplers, limits.maxPerStageDescriptorSampledImages,
			limits.maxPerStageDescriptorUniformBuffers, limits.maxPerStageDescriptorStorageBuffers };
		try
		{
			for (std::size_t index = 0; index < schemas.size(); ++index)
			{
				auto& schema = schemas[index];
				if (schema.Space >= limits.maxBoundDescriptorSets || (index > 0 && schema.Space == schemas[index - 1].Space))
				{
					return false;
				}
				std::sort(schema.Bindings.begin(), schema.Bindings.end(), [](const auto& a, const auto& b) { return a.Binding < b.Binding; });
				for (std::size_t bindingIndex = 0; bindingIndex < schema.Bindings.size(); ++bindingIndex)
				{
					const auto& binding = schema.Bindings[bindingIndex];
					if (binding.Count == 0 || binding.VariableCount || binding.PartiallyBound ||
						(bindingIndex > 0 && binding.Binding == schema.Bindings[bindingIndex - 1].Binding))
					{
						return false;
					}
					const auto type = ToVkDescriptorType(binding.Type);
					ToVkDescriptorStages(binding.Stages);
					const std::size_t slot = type == VK_DESCRIPTOR_TYPE_SAMPLER ? 0 : type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? 1 :
						type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? 2 : 3;
					totals[slot] += binding.Count;
					if (totals[slot] > totalLimits[slot])
					{
						return false;
					}
					for (std::size_t stage = 0; stage < perStage.size(); ++stage)
					{
						if ((static_cast<std::uint32_t>(binding.Stages) & (1u << stage)) != 0)
						{
							perStage[stage][slot] += binding.Count;
							if (perStage[stage][slot] > stageLimits[slot])
							{
								return false;
							}
						}
					}
				}
			}
			for (const auto& counts : perStage)
			{
				if (counts[1] + counts[2] + counts[3] > limits.maxPerStageResources)
				{
					return false;
				}
			}
			if (schemas.empty())
			{
				return true;
			}
			layout.Sets.resize(static_cast<std::size_t>(schemas.back().Space) + 1, VK_NULL_HANDLE);
			for (std::uint32_t space = 0; space < layout.Sets.size(); ++space)
			{
				std::vector<VkDescriptorSetLayoutBinding> bindings;
				if (const auto* schema = FindDescriptorSchema(layout, space))
				{
					for (const auto& binding : schema->Bindings)
					{
						bindings.push_back({ binding.Binding, ToVkDescriptorType(binding.Type), binding.Count, ToVkDescriptorStages(binding.Stages), nullptr });
					}
				}
				VkDescriptorSetLayoutCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = static_cast<std::uint32_t>(bindings.size());
				info.pBindings = bindings.data();
				VkDescriptorSetLayoutSupport support{};
				support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
				layout.Device->Dispatch.vkGetDescriptorSetLayoutSupport(layout.Device->Device.device, &info, &support);
				if (!support.supported || layout.Device->Dispatch.vkCreateDescriptorSetLayout(
					layout.Device->Device.device, &info, nullptr, &layout.Sets[space]) != VK_SUCCESS)
				{
					layout.Sets[space] = VK_NULL_HANDLE;
					return false;
				}
			}
		}
		catch (const std::invalid_argument&)
		{
			return false;
		}
		return true;
	}

} // namespace Swim::RhiVulkan
