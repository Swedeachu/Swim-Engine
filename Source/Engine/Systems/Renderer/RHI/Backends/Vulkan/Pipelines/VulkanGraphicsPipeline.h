#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanPipelineLayout.h"

namespace Swim::RhiVulkan
{

	class VulkanGraphicsPipeline final : public Rhi::GraphicsPipeline
	{
	public:
		VulkanGraphicsPipeline(std::shared_ptr<VulkanDeviceState> state, const Rhi::GraphicsPipelineDesc& desc);
		~VulkanGraphicsPipeline() override;
		static std::unique_ptr<VulkanGraphicsPipeline> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::GraphicsPipelineDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		const std::shared_ptr<VulkanDeviceState>& GetState() const;
		bool MatchesRendering(std::span<const Rhi::Format> colors, Rhi::Format depth, Rhi::SampleCount samples) const;

	private:
		std::shared_ptr<VulkanDeviceState> state;
		std::vector<Rhi::Format> colorFormats;
		Rhi::Format depthFormat;
		Rhi::SampleCount samples;
		VkPipeline pipeline = VK_NULL_HANDLE;
	};

} // namespace Swim::RhiVulkan
