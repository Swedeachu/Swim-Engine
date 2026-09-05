#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	class VulkanSampler final : public Rhi::Sampler
	{
	public:
		VulkanSampler(std::shared_ptr<VulkanDeviceState> state, const Rhi::SamplerDesc& desc);
		~VulkanSampler() override;
		static std::unique_ptr<VulkanSampler> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::SamplerDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		const Rhi::SamplerDesc& GetDesc() const override;
		const std::shared_ptr<VulkanDeviceState>& GetState() const;

	private:
		std::shared_ptr<VulkanDeviceState> state;
		std::string debugName;
		Rhi::SamplerDesc desc;
		VkSampler sampler = VK_NULL_HANDLE;
		bool reserved = false;
	};

} // namespace Swim::RhiVulkan
