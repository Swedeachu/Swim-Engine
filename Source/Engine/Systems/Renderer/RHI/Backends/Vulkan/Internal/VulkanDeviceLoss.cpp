#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceLoss.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <cstdio>

namespace Swim::RhiVulkan
{

	namespace
	{
		const char* FaultStatusName(Rhi::DeviceFaultStatus status)
		{
			switch (status)
			{
			case Rhi::DeviceFaultStatus::None:
				return "none";
			case Rhi::DeviceFaultStatus::Pending:
				return "pending";
			case Rhi::DeviceFaultStatus::Unsupported:
				return "unsupported or disabled";
			case Rhi::DeviceFaultStatus::Complete:
				return "complete";
			case Rhi::DeviceFaultStatus::Truncated:
				return "truncated";
			case Rhi::DeviceFaultStatus::Failed:
				return "failed";
			}
			return "unknown";
		}
	}

	VkResult ObserveVulkanResult(const VulkanDeviceState& state, VkResult result, std::string_view operation) noexcept
	{
		if (result != VK_ERROR_DEVICE_LOST || !state.Diagnostics->TryRecordLoss(operation, result))
		{
			return result;
		}
		char message[384]{};
		std::snprintf(message, sizeof(message), "VK_ERROR_DEVICE_LOST (%d) first observed at %.*s; recreate the device and its resources",
			static_cast<int>(result), static_cast<int>(std::min(operation.size(), std::size_t{ 192 })), operation.empty() ? "" : operation.data());
		if (state.Instance)
		{
			if (state.Instance->Diagnostics.Log)
			{
				state.Instance->Diagnostics.Log->Record(Rhi::DiagnosticSeverity::Error, "DeviceLost", message);
			}
			if (state.Instance->Diagnostics.Echo)
			{
				std::fprintf(stderr, "[Swim Vulkan] %s\n", message);
			}
		}
		// Never from the validation callback. No diagnostic mutex is held across
		// driver calls, and later failures cannot recursively start another capture.
		auto fault = CaptureVulkanDeviceFault(state);
		if (state.Instance && state.Instance->Diagnostics.Log)
		{
			auto& log = *state.Instance->Diagnostics.Log;
			std::snprintf(message, sizeof(message), "fault capture status=%s native result=%d",
				FaultStatusName(fault.Status), static_cast<int>(fault.NativeResult));
			log.Record(Rhi::DiagnosticSeverity::Info, "DeviceFault", message);
			if (!fault.Description.empty())
			{
				log.Record(Rhi::DiagnosticSeverity::Info, "DeviceFault", fault.Description);
			}
			for (const auto& entry : fault.Entries)
			{
				log.Record(Rhi::DiagnosticSeverity::Info, "DeviceFault", entry);
			}
		}
		state.Diagnostics->CompleteFaultCapture(std::move(fault));
		return result;
	}

	VkResult CheckVulkanResult(const VulkanDeviceState& state, VkResult result, std::string_view operation)
	{
		ObserveVulkanResult(state, result, operation);
		if (result == VK_ERROR_DEVICE_LOST)
		{
			throw Rhi::DeviceLostError();
		}
		return result;
	}

	void RequireVulkanDevice(const VulkanDeviceState& state)
	{
		if (state.Diagnostics->IsLost())
		{
			throw Rhi::DeviceLostError();
		}
	}

} // namespace Swim::RhiVulkan
