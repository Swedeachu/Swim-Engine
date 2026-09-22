#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorLayout.h"

#include "Engine/Systems/Renderer/RHI/RhiSampledTexture.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	VulkanPipelineLayoutState::~VulkanPipelineLayoutState()
	{
		if (Device)
		{
			RetireLostVulkanDevice(*Device);
		}
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
		case Rhi::DescriptorType::Sampler:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		case Rhi::DescriptorType::SampledTexture:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case Rhi::DescriptorType::StorageTexture:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case Rhi::DescriptorType::UniformBuffer:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case Rhi::DescriptorType::StorageBuffer:
		case Rhi::DescriptorType::ReadOnlyStorageBuffer:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		default:
			throw std::invalid_argument("Descriptor type is not supported by the graphics resource baseline");
		}
	}

	VkShaderStageFlags ToVkDescriptorStages(Rhi::ShaderStageMask stages)
	{
		const auto mask = static_cast<std::uint32_t>(stages);
		if (mask == 0 || (mask & ~7u) != 0)
		{
			throw std::invalid_argument("Descriptors require explicit vertex/fragment/compute visibility");
		}
		return ((mask & 1u) ? VK_SHADER_STAGE_VERTEX_BIT : 0) | ((mask & 2u) ? VK_SHADER_STAGE_FRAGMENT_BIT : 0) |
			((mask & 4u) ? VK_SHADER_STAGE_COMPUTE_BIT : 0);
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

	bool SameDescriptorBinding(const Rhi::DescriptorBindingDesc& a, const Rhi::DescriptorBindingDesc& b)
	{
		return a.Binding == b.Binding && a.Type == b.Type && a.Count == b.Count && a.Stages == b.Stages &&
			a.VariableCount == b.VariableCount && a.PartiallyBound == b.PartiallyBound &&
			a.StorageTextureFormat == b.StorageTextureFormat && a.SampledClass == b.SampledClass &&
			a.SampledDimension == b.SampledDimension && a.UpdateAfterBind == b.UpdateAfterBind;
	}

	bool AreDescriptorSpacesCompatible(const VulkanPipelineLayoutState& a, const VulkanPipelineLayoutState& b, std::uint32_t space)
	{
		if (&a == &b)
		{
			return true;
		}
		const auto* left = FindDescriptorSchema(a, space);
		const auto* right = FindDescriptorSchema(b, space);
		return left && right && left->Bindings.size() == right->Bindings.size() &&
			std::equal(left->Bindings.begin(), left->Bindings.end(), right->Bindings.begin(), &SameDescriptorBinding);
	}

	namespace
	{
		using Counts = std::array<std::uint64_t, 5>;

		std::size_t GetLimitSlot(VkDescriptorType type)
		{
			return type == VK_DESCRIPTOR_TYPE_SAMPLER		? 0
				: type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE	? 1
				: type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? 2
				: type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ? 3
															: 4;
		}

		// Folds validated explicit spaces over the reflected schemas. Every reflected
		// binding must be representable by its explicit counterpart.
		bool MergeExplicitSpaces(std::vector<Rhi::DescriptorSchemaDesc>& schemas, std::span<const Rhi::DescriptorSchemaDesc> explicitSpaces,
			std::vector<std::uint32_t>& explicitIds)
		{
			for (const auto& space : explicitSpaces)
			{
				if (space.Bindings.empty() || std::find(explicitIds.begin(), explicitIds.end(), space.Space) != explicitIds.end())
				{
					return false;
				}
				explicitIds.push_back(space.Space);
				auto reflected = std::find_if(schemas.begin(), schemas.end(),
					[&](const auto& schema)
					{
						return schema.Space == space.Space;
					});
				if (reflected != schemas.end())
				{
					for (const auto& binding : reflected->Bindings)
					{
						const auto found = std::find_if(space.Bindings.begin(), space.Bindings.end(),
							[&](const auto& candidate)
							{
								return candidate.Binding == binding.Binding;
							});
						if (found == space.Bindings.end() || found->Type != binding.Type || found->SampledClass != binding.SampledClass ||
							found->SampledDimension != binding.SampledDimension ||
							found->StorageTextureFormat != binding.StorageTextureFormat ||
							(static_cast<std::uint32_t>(found->Stages) & static_cast<std::uint32_t>(binding.Stages)) !=
								static_cast<std::uint32_t>(binding.Stages) ||
							(binding.Count == 0 ? !(found->PartiallyBound && found->UpdateAfterBind) : found->Count != binding.Count))
						{
							return false;
						}
					}
					reflected->Bindings = space.Bindings;
				}
				else
				{
					schemas.push_back(space);
				}
			}
			return true;
		}
	} // namespace

	bool CreateDescriptorLayouts(VulkanPipelineLayoutState& layout, std::span<const Rhi::DescriptorSchemaDesc> explicitSpaces)
	{
		auto& device = *layout.Device;
		const auto& limits = device.Device.physical_device.properties.limits;
		const auto& indexing = device.DescriptorIndexing;
		auto& schemas = layout.Interface.DescriptorSchemas;
		std::vector<std::uint32_t> explicitIds;
		if (!MergeExplicitSpaces(schemas, explicitSpaces, explicitIds))
		{
			return false;
		}
		std::sort(schemas.begin(), schemas.end(),
			[](const auto& a, const auto& b)
			{
				return a.Space < b.Space;
			});
		// Plain limits count sets without update-after-bind bindings; the
		// update-after-bind limits count every set once any such set exists.
		Counts plainTotals{};
		Counts allTotals{};
		std::array<Counts, 3> plainStage{};
		std::array<Counts, 3> allStage{};
		bool anyUpdateAfterBind = false;
		const Counts totalLimits{ limits.maxDescriptorSetSamplers, limits.maxDescriptorSetSampledImages,
			limits.maxDescriptorSetUniformBuffers, limits.maxDescriptorSetStorageBuffers, limits.maxDescriptorSetStorageImages };
		const Counts stageLimits{ limits.maxPerStageDescriptorSamplers, limits.maxPerStageDescriptorSampledImages,
			limits.maxPerStageDescriptorUniformBuffers, limits.maxPerStageDescriptorStorageBuffers,
			limits.maxPerStageDescriptorStorageImages };
		const Counts bindTotalLimits{ indexing.maxDescriptorSetUpdateAfterBindSamplers,
			indexing.maxDescriptorSetUpdateAfterBindSampledImages, indexing.maxDescriptorSetUpdateAfterBindUniformBuffers,
			indexing.maxDescriptorSetUpdateAfterBindStorageBuffers, indexing.maxDescriptorSetUpdateAfterBindStorageImages };
		const Counts bindStageLimits{ indexing.maxPerStageDescriptorUpdateAfterBindSamplers,
			indexing.maxPerStageDescriptorUpdateAfterBindSampledImages, indexing.maxPerStageDescriptorUpdateAfterBindUniformBuffers,
			indexing.maxPerStageDescriptorUpdateAfterBindStorageBuffers, indexing.maxPerStageDescriptorUpdateAfterBindStorageImages };
		try
		{
			for (std::size_t index = 0; index < schemas.size(); ++index)
			{
				auto& schema = schemas[index];
				if (schema.Space >= limits.maxBoundDescriptorSets || (index > 0 && schema.Space == schemas[index - 1].Space))
				{
					return false;
				}
				const bool explicitSpace = std::find(explicitIds.begin(), explicitIds.end(), schema.Space) != explicitIds.end();
				std::sort(schema.Bindings.begin(), schema.Bindings.end(),
					[](const auto& a, const auto& b)
					{
						return a.Binding < b.Binding;
					});
				const bool setUpdateAfterBind = std::any_of(schema.Bindings.begin(), schema.Bindings.end(),
					[](const auto& binding)
					{
						return binding.UpdateAfterBind;
					});
				anyUpdateAfterBind = anyUpdateAfterBind || setUpdateAfterBind;
				for (std::size_t bindingIndex = 0; bindingIndex < schema.Bindings.size(); ++bindingIndex)
				{
					const auto& binding = schema.Bindings[bindingIndex];
					const bool bindlessType =
						binding.Type == Rhi::DescriptorType::Sampler || binding.Type == Rhi::DescriptorType::SampledTexture;
					// Count 0 is a runtime-sized reflected array that no explicit space sized.
					if (binding.Count == 0 || binding.VariableCount ||
						(bindingIndex > 0 && binding.Binding == schema.Bindings[bindingIndex - 1].Binding) ||
						((binding.PartiallyBound || binding.UpdateAfterBind) && !bindlessType) ||
						(binding.UpdateAfterBind && (!binding.PartiallyBound || !device.BindlessDescriptorsEnabled)))
					{
						return false;
					}
					if (binding.Type == Rhi::DescriptorType::SampledTexture
							? (!Rhi::IsSampledTextureDimension(binding.SampledDimension) ||
								  (binding.SampledDimension == Rhi::TextureViewDimension::TextureCubeArray &&
									  !device.Device.physical_device.features.imageCubeArray))
							: binding.SampledDimension != Rhi::TextureViewDimension::Texture2D)
					{
						return false;
					}
					const auto type = ToVkDescriptorType(binding.Type);
					ToVkDescriptorStages(binding.Stages);
					// Explicit (shared) spaces may be visible to stages this program lacks.
					if ((!explicitSpace &&
							(static_cast<std::uint32_t>(binding.Stages) & ~static_cast<std::uint32_t>(layout.ProgramStages)) != 0) ||
						((binding.Type == Rhi::DescriptorType::StorageBuffer || binding.Type == Rhi::DescriptorType::StorageTexture) &&
							binding.Stages != Rhi::ShaderStageMask::Compute) ||
						(binding.Type == Rhi::DescriptorType::StorageTexture ? !Rhi::IsStorageTextureFormat(binding.StorageTextureFormat)
																			 : binding.StorageTextureFormat != Rhi::Format::Undefined) ||
						(binding.Type == Rhi::DescriptorType::SampledTexture ? (binding.SampledClass != Rhi::SampledTextureClass::Float &&
																				   binding.SampledClass != Rhi::SampledTextureClass::Uint &&
																				   binding.SampledClass != Rhi::SampledTextureClass::Sint)
																			 : binding.SampledClass != Rhi::SampledTextureClass::Float))
					{
						return false;
					}
					const std::size_t slot = GetLimitSlot(type);
					const auto accumulate = [&](Counts& totals, std::array<Counts, 3>& perStage)
					{
						totals[slot] += binding.Count;
						for (std::size_t stage = 0; stage < perStage.size(); ++stage)
						{
							if ((static_cast<std::uint32_t>(binding.Stages) & (1u << stage)) != 0)
							{
								perStage[stage][slot] += binding.Count;
							}
						}
					};
					if (!setUpdateAfterBind)
					{
						accumulate(plainTotals, plainStage);
					}
					accumulate(allTotals, allStage);
				}
			}
			const auto withinLimits = [](const Counts& totals, const std::array<Counts, 3>& perStage, const Counts& totalLimit,
										  const Counts& stageLimit, std::uint64_t stageResources)
			{
				for (std::size_t slot = 0; slot < totals.size(); ++slot)
				{
					if (totals[slot] > totalLimit[slot])
					{
						return false;
					}
				}
				for (const auto& counts : perStage)
				{
					for (std::size_t slot = 0; slot < counts.size(); ++slot)
					{
						if (counts[slot] > stageLimit[slot])
						{
							return false;
						}
					}
					if (counts[1] + counts[2] + counts[3] + counts[4] > stageResources)
					{
						return false;
					}
				}
				return true;
			};
			if (!withinLimits(plainTotals, plainStage, totalLimits, stageLimits, limits.maxPerStageResources) ||
				(anyUpdateAfterBind &&
					!withinLimits(allTotals, allStage, bindTotalLimits, bindStageLimits, indexing.maxPerStageUpdateAfterBindResources)))
			{
				return false;
			}
			if (schemas.empty())
			{
				return true;
			}
			layout.Sets.resize(static_cast<std::size_t>(schemas.back().Space) + 1, VK_NULL_HANDLE);
			layout.UpdateAfterBindSets.assign(layout.Sets.size(), false);
			for (std::uint32_t space = 0; space < layout.Sets.size(); ++space)
			{
				std::vector<VkDescriptorSetLayoutBinding> bindings;
				std::vector<VkDescriptorBindingFlags> flags;
				bool updateAfterBind = false;
				if (const auto* schema = FindDescriptorSchema(layout, space))
				{
					for (const auto& binding : schema->Bindings)
					{
						bindings.push_back({ binding.Binding, ToVkDescriptorType(binding.Type), binding.Count,
							ToVkDescriptorStages(binding.Stages), nullptr });
						VkDescriptorBindingFlags bindingFlags = 0;
						if (binding.PartiallyBound)
						{
							bindingFlags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
						}
						if (binding.UpdateAfterBind)
						{
							bindingFlags |=
								VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
							updateAfterBind = true;
						}
						flags.push_back(bindingFlags);
					}
				}
				VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
				flagInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
				flagInfo.bindingCount = static_cast<std::uint32_t>(flags.size());
				flagInfo.pBindingFlags = flags.data();
				VkDescriptorSetLayoutCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = static_cast<std::uint32_t>(bindings.size());
				info.pBindings = bindings.data();
				// Only chain binding flags when some are set, keeping plain layouts unchanged.
				if (std::any_of(flags.begin(), flags.end(),
						[](auto value)
						{
							return value != 0;
						}))
				{
					info.pNext = &flagInfo;
				}
				if (updateAfterBind)
				{
					info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
				}
				layout.UpdateAfterBindSets[space] = updateAfterBind;
				VkDescriptorSetLayoutSupport support{};
				support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
				device.Dispatch.vkGetDescriptorSetLayoutSupport(device.Device.device, &info, &support);
				if (!support.supported)
				{
					return false;
				}
				const auto createResult =
					device.Dispatch.vkCreateDescriptorSetLayout(device.Device.device, &info, nullptr, &layout.Sets[space]);
				if (createResult != VK_SUCCESS)
				{
					layout.Sets[space] = VK_NULL_HANDLE;
					CheckVulkanResult(device, createResult, "vkCreateDescriptorSetLayout");
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
