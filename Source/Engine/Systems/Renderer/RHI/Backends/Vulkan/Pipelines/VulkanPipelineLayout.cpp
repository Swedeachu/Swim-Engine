#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanPipelineLayout.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

namespace Swim::RhiVulkan
{

	VulkanPipelineLayout::VulkanPipelineLayout(std::shared_ptr<VulkanDeviceState> state, VulkanShaderProgram& program)
		: state(std::move(state)), program(program), interface(program.GetInterface())
	{
	}

	VulkanPipelineLayout::~VulkanPipelineLayout()
	{
		if (layout != VK_NULL_HANDLE)
		{
			state->Dispatch.vkDestroyPipelineLayout(state->Device.device, layout, nullptr);
		}
	}

	std::unique_ptr<VulkanPipelineLayout> VulkanPipelineLayout::Create(
		std::shared_ptr<VulkanDeviceState> state, const Rhi::PipelineLayoutDesc& desc)
	{
		auto* program = dynamic_cast<VulkanShaderProgram*>(desc.Program);
		if (program == nullptr || program->GetState() != state ||
			!program->GetInterface().DescriptorSchemas.empty() || !program->GetInterface().PushConstants.empty())
		{
			// Resource bindings land with the textured-draw checkpoint. Never discard reflection.
			return nullptr;
		}
		auto result = std::make_unique<VulkanPipelineLayout>(std::move(state), *program);
		VkPipelineLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		if (result->state->Dispatch.vkCreatePipelineLayout(result->state->Device.device, &info, nullptr, &result->layout) != VK_SUCCESS)
		{
			result->layout = VK_NULL_HANDLE;
			return nullptr;
		}
		return result;
	}

	std::uintptr_t VulkanPipelineLayout::GetNativeHandle() const
	{
		return ToNativeHandle(layout);
	}

	Rhi::ShaderProgram& VulkanPipelineLayout::GetProgram() const
	{
		return program;
	}

	const Rhi::ShaderProgramInterface& VulkanPipelineLayout::GetInterface() const
	{
		return interface;
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanPipelineLayout::GetState() const
	{
		return state;
	}

} // namespace Swim::RhiVulkan
