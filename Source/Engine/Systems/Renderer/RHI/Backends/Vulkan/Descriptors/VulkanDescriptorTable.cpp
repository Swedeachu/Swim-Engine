#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorTable.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

#include <algorithm>

namespace Swim::RhiVulkan
{

	VulkanDescriptorTable::VulkanDescriptorTable(VulkanPipelineLayout& layout, std::uint32_t space)
		: layout(layout), layoutState(layout.GetLayoutState()), space(space)
	{
	}

	VulkanDescriptorTable::~VulkanDescriptorTable()
	{
		if (pool != VK_NULL_HANDLE)
		{
			GetState()->Dispatch.vkDestroyDescriptorPool(GetState()->Device.device, pool, nullptr);
		}
	}

	std::unique_ptr<VulkanDescriptorTable> VulkanDescriptorTable::Create(
		std::shared_ptr<VulkanDeviceState> state, const Rhi::DescriptorTableDesc& desc)
	{
		auto* layout = dynamic_cast<VulkanPipelineLayout*>(desc.Layout);
		if (layout == nullptr || layout->GetState() != state || desc.VariableDescriptorCount != 0)
		{
			return nullptr;
		}
		const auto* schema = FindDescriptorSchema(*layout->GetLayoutState(), desc.Space);
		if (schema == nullptr || schema->Bindings.empty())
		{
			return nullptr;
		}
		auto result = std::make_unique<VulkanDescriptorTable>(*layout, desc.Space);
		std::vector<VkDescriptorPoolSize> sizes;
		for (const auto& binding : schema->Bindings)
		{
			const auto type = ToVkDescriptorType(binding.Type);
			const auto found = std::find_if(sizes.begin(), sizes.end(), [type](const auto& size) { return size.type == type; });
			if (found == sizes.end())
			{
				sizes.push_back({ type, binding.Count });
			}
			else
			{
				found->descriptorCount += binding.Count;
			}
			result->initialized.emplace_back(binding.Count, false);
		}
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
		poolInfo.pPoolSizes = sizes.data();
		if (state->Dispatch.vkCreateDescriptorPool(state->Device.device, &poolInfo, nullptr, &result->pool) != VK_SUCCESS)
		{
			result->pool = VK_NULL_HANDLE;
			return nullptr;
		}
		VkDescriptorSetAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		info.descriptorPool = result->pool;
		info.descriptorSetCount = 1;
		info.pSetLayouts = &result->layoutState->Sets[desc.Space];
		if (state->Dispatch.vkAllocateDescriptorSets(state->Device.device, &info, &result->set) != VK_SUCCESS)
		{
			return nullptr;
		}
		SetVulkanObjectName(*state, VK_OBJECT_TYPE_DESCRIPTOR_POOL, ToNativeHandle(result->pool), desc.DebugName);
		SetVulkanObjectName(*state, VK_OBJECT_TYPE_DESCRIPTOR_SET, ToNativeHandle(result->set), desc.DebugName);
		return result;
	}

	std::uintptr_t VulkanDescriptorTable::GetNativeHandle() const
	{
		return ToNativeHandle(set);
	}

	Rhi::PipelineLayout& VulkanDescriptorTable::GetLayout() const
	{
		return layout;
	}

	std::uint32_t VulkanDescriptorTable::GetSpace() const
	{
		return space;
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanDescriptorTable::GetState() const
	{
		return layoutState->Device;
	}

	const std::shared_ptr<VulkanPipelineLayoutState>& VulkanDescriptorTable::GetLayoutState() const
	{
		return layoutState;
	}

	bool VulkanDescriptorTable::IsComplete() const
	{
		for (const auto& binding : initialized)
		{
			if (std::find(binding.begin(), binding.end(), false) != binding.end())
			{
				return false;
			}
		}
		return true;
	}

	void VulkanDescriptorTable::Seal()
	{
		sealed.store(true);
	}

} // namespace Swim::RhiVulkan
