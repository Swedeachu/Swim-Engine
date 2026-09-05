#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanShaderProgram.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorLayout.h"

namespace Swim::RhiVulkan
{

	// Program outlives its layout, as required by PipelineLayout::GetProgram.
	class VulkanPipelineLayout final : public Rhi::PipelineLayout
	{
	public:
		VulkanPipelineLayout(std::shared_ptr<VulkanDeviceState> state, VulkanShaderProgram& program);
		~VulkanPipelineLayout() override = default;
		const std::shared_ptr<VulkanPipelineLayoutState>& GetLayoutState() const;
		static std::unique_ptr<VulkanPipelineLayout> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::PipelineLayoutDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		Rhi::ShaderProgram& GetProgram() const override;
		const Rhi::ShaderProgramInterface& GetInterface() const override;
		const std::shared_ptr<VulkanDeviceState>& GetState() const;

	private:
		std::shared_ptr<VulkanDeviceState> state;
		VulkanShaderProgram& program;
		std::shared_ptr<VulkanPipelineLayoutState> layoutState;
	};

} // namespace Swim::RhiVulkan
