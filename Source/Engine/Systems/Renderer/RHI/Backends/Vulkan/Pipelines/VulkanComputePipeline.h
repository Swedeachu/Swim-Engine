#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanPipelineLayout.h"

namespace Swim::RhiVulkan
{

	class VulkanComputePipeline final : public Rhi::ComputePipeline
	{
	public:
		VulkanComputePipeline(std::shared_ptr<VulkanDeviceState> state, std::shared_ptr<VulkanPipelineLayoutState> layout);
		~VulkanComputePipeline() override;
		static std::unique_ptr<VulkanComputePipeline> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::ComputePipelineDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		const std::shared_ptr<VulkanDeviceState>& GetState() const;
		const std::shared_ptr<VulkanPipelineLayoutState>& GetLayoutState() const;

	private:
		std::shared_ptr<VulkanDeviceState> state;
		std::shared_ptr<VulkanPipelineLayoutState> layoutState;
		VkPipeline pipeline = VK_NULL_HANDLE;
	};

} // namespace Swim::RhiVulkan
