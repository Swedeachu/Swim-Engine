#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	Rhi::TimestampInfo GetVulkanTimestampInfo(const VulkanDeviceState& state, std::uint32_t family);

	class VulkanQueryPool final : public Rhi::QueryPool
	{
	public:
		~VulkanQueryPool() override;
		static std::unique_ptr<VulkanQueryPool> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::QueryPoolDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		const Rhi::QueryPoolDesc& GetDesc() const override;
		Rhi::TimestampInfo GetTimestampInfo() const override;
		Rhi::QueryReadStatus ReadTimestamps(std::uint32_t first, std::span<Rhi::TimestampResult> results) override;
		bool Matches(const VulkanDeviceState& device, std::uint32_t family) const;
		bool Contains(std::uint32_t first, std::size_t count) const;

	private:
		VulkanQueryPool(std::shared_ptr<VulkanDeviceState> state, const Rhi::QueryPoolDesc& desc, std::uint32_t family);
		std::shared_ptr<VulkanDeviceState> state;
		std::string debugName;
		Rhi::QueryPoolDesc desc;
		std::uint32_t family;
		VkQueryPool pool = VK_NULL_HANDLE;
	};

} // namespace Swim::RhiVulkan
