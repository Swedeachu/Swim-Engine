#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Queries/VulkanQueryPool.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

#include <algorithm>
#include <array>

namespace Swim::RhiVulkan
{

	Rhi::TimestampInfo GetVulkanTimestampInfo(const VulkanDeviceState& state, std::uint32_t family)
	{
		if (family >= state.QueueProperties.size())
		{
			return {};
		}
		// GPU query reset requires graphics or compute support. A transfer-only
		// family cannot provide this RHI query lifecycle even if it can timestamp.
		const auto& properties = state.QueueProperties[family];
		if (properties.queueCount == 0 ||
			(properties.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
		{
			return {};
		}
		Rhi::TimestampInfo info{ state.Device.physical_device.properties.limits.timestampPeriod, properties.timestampValidBits };
		return info.IsSupported() ? info : Rhi::TimestampInfo{};
	}

	VulkanQueryPool::VulkanQueryPool(std::shared_ptr<VulkanDeviceState> state, const Rhi::QueryPoolDesc& desc, std::uint32_t family)
		: state(std::move(state)), debugName(desc.DebugName), desc(desc), family(family)
	{
		this->desc.DebugName = debugName;
	}

	VulkanQueryPool::~VulkanQueryPool()
	{
		if (pool != VK_NULL_HANDLE)
		{
			state->Dispatch.vkDestroyQueryPool(state->Device.device, pool, nullptr);
		}
	}

	std::unique_ptr<VulkanQueryPool> VulkanQueryPool::Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::QueryPoolDesc& desc)
	{
		if (!state || desc.Type != Rhi::QueryType::Timestamp || desc.Count == 0)
		{
			return nullptr;
		}
		std::uint32_t family = UINT32_MAX;
		switch (desc.Queue)
		{
		case Rhi::QueueType::Graphics:
			family = state->QueueFamilies.Graphics;
			break;
		case Rhi::QueueType::Compute:
			family = state->QueueFamilies.Compute;
			break;
		case Rhi::QueueType::Transfer:
			family = state->QueueFamilies.Transfer;
			break;
		default:
			return nullptr;
		}
		if (!GetVulkanTimestampInfo(*state, family).IsSupported())
		{
			return nullptr;
		}
		auto result = std::unique_ptr<VulkanQueryPool>(new VulkanQueryPool(state, desc, family));
		VkQueryPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = desc.Count;
		if (state->Dispatch.vkCreateQueryPool(state->Device.device, &info, nullptr, &result->pool) != VK_SUCCESS)
		{
			result->pool = VK_NULL_HANDLE;
			return nullptr;
		}
		SetVulkanObjectName(*state, VK_OBJECT_TYPE_QUERY_POOL, ToNativeHandle(result->pool), result->debugName);
		return result;
	}

	std::uintptr_t VulkanQueryPool::GetNativeHandle() const
	{
		return ToNativeHandle(pool);
	}

	const Rhi::QueryPoolDesc& VulkanQueryPool::GetDesc() const
	{
		return desc;
	}

	Rhi::TimestampInfo VulkanQueryPool::GetTimestampInfo() const
	{
		return GetVulkanTimestampInfo(*state, family);
	}

	bool VulkanQueryPool::Matches(const VulkanDeviceState& device, std::uint32_t queueFamily) const
	{
		return state.get() == &device && family == queueFamily;
	}

	bool VulkanQueryPool::Contains(std::uint32_t first, std::size_t count) const
	{
		return count > 0 && first < desc.Count && count <= desc.Count - first;
	}

	Rhi::QueryReadStatus VulkanQueryPool::ReadTimestamps(std::uint32_t first, std::span<Rhi::TimestampResult> results)
	{
		std::fill(results.begin(), results.end(), Rhi::TimestampResult{});
		if (!Contains(first, results.size()))
		{
			return Rhi::QueryReadStatus::Error;
		}
		// Native payload: 64-bit value followed by 64-bit availability. Do not
		// expose unspecified values for unavailable queries, including on failure.
		using NativeResult = std::array<std::uint64_t, 2>;
		std::vector<NativeResult> native(results.size());
		const auto status = state->Dispatch.vkGetQueryPoolResults(state->Device.device, pool, first,
			static_cast<std::uint32_t>(results.size()), native.size() * sizeof(NativeResult), native.data(),
			sizeof(NativeResult), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		if (status != VK_SUCCESS && status != VK_NOT_READY)
		{
			return Rhi::QueryReadStatus::Error;
		}
		bool ready = status == VK_SUCCESS;
		for (std::size_t index = 0; index < results.size(); ++index)
		{
			if (native[index][1] != 0)
			{
				results[index] = { native[index][0], true };
			}
			else
			{
				ready = false;
			}
		}
		return ready ? Rhi::QueryReadStatus::Ready : Rhi::QueryReadStatus::NotReady;
	}

} // namespace Swim::RhiVulkan
