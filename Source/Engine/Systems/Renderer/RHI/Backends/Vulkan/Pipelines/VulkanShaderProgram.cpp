#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanShaderProgram.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

#include <cstring>

namespace Swim::RhiVulkan
{

	VulkanShaderProgram::VulkanShaderProgram(std::shared_ptr<VulkanDeviceState> state)
		: state(std::move(state))
	{
	}

	VulkanShaderProgram::~VulkanShaderProgram()
	{
		for (const auto& stage : stages)
		{
			if (stage.Module != VK_NULL_HANDLE)
			{
				state->Dispatch.vkDestroyShaderModule(state->Device.device, stage.Module, nullptr);
			}
		}
	}

	std::unique_ptr<VulkanShaderProgram> VulkanShaderProgram::Create(
		std::shared_ptr<VulkanDeviceState> state, const Rhi::ShaderProgramDesc& desc)
	{
		if (desc.Stages.empty() || desc.Stages.size() > 2)
		{
			return nullptr;
		}
		auto program = std::make_unique<VulkanShaderProgram>(std::move(state));
		program->interface.DescriptorSchemas.assign(desc.Interface.DescriptorSchemas.begin(), desc.Interface.DescriptorSchemas.end());
		program->interface.PushConstants.assign(desc.Interface.PushConstants.begin(), desc.Interface.PushConstants.end());
		program->stages.resize(desc.Stages.size());
		std::uint32_t seenStages = 0;
		for (std::size_t index = 0; index < desc.Stages.size(); ++index)
		{
			const auto& source = desc.Stages[index];
			const auto bit = static_cast<std::uint32_t>(source.Stage);
			// Additional shader stages require explicit device features and pipeline support.
			if ((source.Stage != Rhi::ShaderStageMask::Vertex && source.Stage != Rhi::ShaderStageMask::Fragment) ||
				(seenStages & bit) != 0 || source.EntryPoint.empty() || source.EntryPoint.find('\0') != std::string_view::npos ||
				source.Bytecode.size() < 5 * sizeof(std::uint32_t) || source.Bytecode.size() % sizeof(std::uint32_t) != 0)
			{
				return nullptr;
			}
			seenStages |= bit;
			// Public byte spans need not be aligned; Vulkan requires aligned uint32_t words.
			std::vector<std::uint32_t> words(source.Bytecode.size() / sizeof(std::uint32_t));
			std::memcpy(words.data(), source.Bytecode.data(), source.Bytecode.size());
			if (words[0] != 0x07230203 || words[1] < 0x00010000 || words[1] > 0x00010600 || words[3] == 0 || words[4] != 0)
			{
				return nullptr;
			}
			auto& stage = program->stages[index];
			stage.Stage = source.Stage == Rhi::ShaderStageMask::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
			stage.EntryPoint = source.EntryPoint;
			VkShaderModuleCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = source.Bytecode.size();
			info.pCode = words.data();
			if (program->state->Dispatch.vkCreateShaderModule(program->state->Device.device, &info, nullptr, &stage.Module) != VK_SUCCESS)
			{
				stage.Module = VK_NULL_HANDLE;
				return nullptr;
			}
			SetVulkanObjectName(*program->state, VK_OBJECT_TYPE_SHADER_MODULE, ToNativeHandle(stage.Module), desc.DebugName);
		}
		return program;
	}

	std::uintptr_t VulkanShaderProgram::GetNativeHandle() const
	{
		// A program owns multiple modules; there is no single native program handle.
		return 0;
	}

	const Rhi::ShaderProgramInterface& VulkanShaderProgram::GetInterface() const
	{
		return interface;
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanShaderProgram::GetState() const
	{
		return state;
	}

	const std::vector<VulkanShaderStage>& VulkanShaderProgram::GetStages() const
	{
		return stages;
	}

} // namespace Swim::RhiVulkan
