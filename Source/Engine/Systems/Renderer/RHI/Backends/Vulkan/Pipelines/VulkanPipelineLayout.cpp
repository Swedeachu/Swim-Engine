#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanPipelineLayout.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

namespace Swim::RhiVulkan
{

	VulkanPipelineLayout::VulkanPipelineLayout(std::shared_ptr<VulkanDeviceState> state, VulkanShaderProgram& program)
		: state(std::move(state)), program(program), layoutState(std::make_shared<VulkanPipelineLayoutState>())
	{
		layoutState->Device = this->state;
		layoutState->Interface = program.GetInterface();
	}

	const std::shared_ptr<VulkanPipelineLayoutState>& VulkanPipelineLayout::GetLayoutState() const
	{
		return layoutState;
	}

	std::unique_ptr<VulkanPipelineLayout> VulkanPipelineLayout::Create(
		std::shared_ptr<VulkanDeviceState> state, const Rhi::PipelineLayoutDesc& desc)
	{
		auto* program = dynamic_cast<VulkanShaderProgram*>(desc.Program);
		if (program == nullptr || program->GetState() != state ||
			!program->GetInterface().PushConstants.empty())
		{
			// Push-constant recording is a separate contract extension. Never discard reflection.
			return nullptr;
		}
		auto result = std::make_unique<VulkanPipelineLayout>(std::move(state), *program);
		if (!CreateDescriptorLayouts(*result->layoutState))
		{
			return nullptr;
		}
		VkPipelineLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		info.setLayoutCount = static_cast<std::uint32_t>(result->layoutState->Sets.size());
		info.pSetLayouts = result->layoutState->Sets.data();
		if (result->state->Dispatch.vkCreatePipelineLayout(result->state->Device.device, &info, nullptr, &result->layoutState->Layout) != VK_SUCCESS)
		{
			result->layoutState->Layout = VK_NULL_HANDLE;
			return nullptr;
		}
		return result;
	}

	std::uintptr_t VulkanPipelineLayout::GetNativeHandle() const
	{
		return ToNativeHandle(layoutState->Layout);
	}

	Rhi::ShaderProgram& VulkanPipelineLayout::GetProgram() const
	{
		return program;
	}

	const Rhi::ShaderProgramInterface& VulkanPipelineLayout::GetInterface() const
	{
		return layoutState->Interface;
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanPipelineLayout::GetState() const
	{
		return state;
	}

} // namespace Swim::RhiVulkan
