#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <string>
#include <vector>

namespace Swim::RhiVulkan
{

	struct VulkanShaderStage
	{
		VkShaderStageFlagBits Stage = VK_SHADER_STAGE_VERTEX_BIT;
		VkShaderModule Module = VK_NULL_HANDLE;
		std::string EntryPoint;
	};

	class VulkanShaderProgram final : public Rhi::ShaderProgram
	{
	public:
		explicit VulkanShaderProgram(std::shared_ptr<VulkanDeviceState> state);
		~VulkanShaderProgram() override;
		static std::unique_ptr<VulkanShaderProgram> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::ShaderProgramDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		const Rhi::ShaderProgramInterface& GetInterface() const override;
		const std::shared_ptr<VulkanDeviceState>& GetState() const;
		const std::vector<VulkanShaderStage>& GetStages() const;

	private:
		std::shared_ptr<VulkanDeviceState> state;
		Rhi::ShaderProgramInterface interface;
		std::vector<VulkanShaderStage> stages;
	};

} // namespace Swim::RhiVulkan
