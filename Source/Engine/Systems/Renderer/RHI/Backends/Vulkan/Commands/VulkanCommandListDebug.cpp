#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Swim::RhiVulkan
{

	namespace
	{
		std::string ValidateLabel(std::string_view name, const std::array<float, 4>& color)
		{
			if (name.empty() || name.find('\0') != std::string_view::npos ||
				std::any_of(color.begin(), color.end(), [](float value) { return !std::isfinite(value) || value < 0 || value > 1; }))
			{
				throw std::invalid_argument("RHI debug labels require a name and finite normalized RGBA color");
			}
			return std::string(name);
		}

		bool HasLabelRegions(const VulkanDeviceState& state)
		{
			return state.Instance && state.Instance->Diagnostics.DebugUtilsEnabled &&
				state.Instance->Dispatch.vkCmdBeginDebugUtilsLabelEXT && state.Instance->Dispatch.vkCmdEndDebugUtilsLabelEXT;
		}
	}

	void VulkanCommandList::BeginDebugLabel(std::string_view name, const std::array<float, 4>& color)
	{
		RequireRecording();
		const auto owned = ValidateLabel(name, color);
		if (debugLabelDepth == UINT32_MAX)
		{
			throw std::logic_error("RHI debug label nesting overflow");
		}
		if (HasLabelRegions(*GetState()))
		{
			VkDebugUtilsLabelEXT info{};
			info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
			info.pLabelName = owned.c_str();
			std::copy(color.begin(), color.end(), info.color);
			GetState()->Instance->Dispatch.vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &info);
		}
		++debugLabelDepth;
	}

	void VulkanCommandList::EndDebugLabel()
	{
		RequireRecording();
		if (debugLabelDepth == 0)
		{
			throw std::logic_error("RHI debug label region stack is empty");
		}
		if (HasLabelRegions(*GetState()))
		{
			GetState()->Instance->Dispatch.vkCmdEndDebugUtilsLabelEXT(commandBuffer);
		}
		--debugLabelDepth;
	}

	void VulkanCommandList::InsertDebugLabel(std::string_view name, const std::array<float, 4>& color)
	{
		RequireRecording();
		const auto owned = ValidateLabel(name, color);
		const auto& state = *GetState();
		if (state.Instance && state.Instance->Diagnostics.DebugUtilsEnabled && state.Instance->Dispatch.vkCmdInsertDebugUtilsLabelEXT)
		{
			VkDebugUtilsLabelEXT info{};
			info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
			info.pLabelName = owned.c_str();
			std::copy(color.begin(), color.end(), info.color);
			state.Instance->Dispatch.vkCmdInsertDebugUtilsLabelEXT(commandBuffer, &info);
		}
	}

} // namespace Swim::RhiVulkan
