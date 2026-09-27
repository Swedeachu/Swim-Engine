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
		const auto& state = GetState();
		RetireLostVulkanDevice(*state);
		if (pool == VK_NULL_HANDLE)
		{
			return;
		}
		// Keep the pool for the next table with the same signature (its set is freed by the
		// reset). The caller's contract is unchanged: the GPU no longer uses the table.
		if (!state->Diagnostics->IsLost() && state->Dispatch.vkResetDescriptorPool != nullptr &&
			state->Dispatch.vkResetDescriptorPool(state->Device.device, pool, 0) == VK_SUCCESS)
		{
			std::lock_guard lock(state->DescriptorPoolMutex);
			auto& free = state->FreeDescriptorPools[poolSignature];
			if (free.size() < 1024)
			{
				free.push_back(pool);
				return;
			}
		}
		state->Dispatch.vkDestroyDescriptorPool(state->Device.device, pool, nullptr);
	}

	std::unique_ptr<VulkanDescriptorTable> VulkanDescriptorTable::Create(
		std::shared_ptr<VulkanDeviceState> state, const Rhi::DescriptorTableDesc& desc)
	{
		if (state)
		{
			RequireVulkanDevice(*state);
		}
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
		if (result->layoutState->UpdateAfterBindSets[desc.Space])
		{
			poolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		}
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
		poolInfo.pPoolSizes = sizes.data();
		result->poolSignature.push_back(static_cast<std::uint32_t>(poolInfo.flags));
		for (const auto& size : sizes)
		{
			result->poolSignature.push_back(static_cast<std::uint32_t>(size.type));
			result->poolSignature.push_back(size.descriptorCount);
		}
		bool recycled = false;
		{
			std::lock_guard lock(state->DescriptorPoolMutex);
			const auto found = state->FreeDescriptorPools.find(result->poolSignature);
			if (found != state->FreeDescriptorPools.end() && !found->second.empty())
			{
				result->pool = found->second.back();
				found->second.pop_back();
				recycled = true;
			}
		}
		if (!recycled)
		{
			const auto createResult = state->Dispatch.vkCreateDescriptorPool(state->Device.device, &poolInfo, nullptr, &result->pool);
			if (createResult != VK_SUCCESS)
			{
				result->pool = VK_NULL_HANDLE;
				CheckVulkanResult(*state, createResult, "vkCreateDescriptorPool");
				return nullptr;
			}
		}
		VkDescriptorSetAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		info.descriptorPool = result->pool;
		info.descriptorSetCount = 1;
		info.pSetLayouts = &result->layoutState->Sets[desc.Space];
		if (CheckVulkanResult(*state, state->Dispatch.vkAllocateDescriptorSets(state->Device.device, &info, &result->set), "vkAllocateDescriptorSets") != VK_SUCCESS)
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
		// Partially bound elements only need a descriptor before shaders access them.
		const auto& bindings = FindDescriptorSchema(*layoutState, space)->Bindings;
		for (std::size_t index = 0; index < initialized.size(); ++index)
		{
			if (!bindings[index].PartiallyBound && std::find(initialized[index].begin(), initialized[index].end(), false) != initialized[index].end())
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
