#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

namespace Swim::RhiVulkan
{

	namespace
	{
		// The retirement mutex serializes device-wide waits. Lock each selected
		// native queue once, accounting for graphics/compute/transfer aliases.
		auto LockQueues(const VulkanDeviceState& state)
		{
			std::array<std::unique_lock<std::mutex>, 3> locks;
			for (std::size_t index = 0; index < state.QueueMutexes.size(); ++index)
			{
				const auto& mutex = state.QueueMutexes[index];
				bool duplicate = false;
				for (std::size_t previous = 0; previous < index; ++previous)
				{
					duplicate = duplicate || state.QueueMutexes[previous] == mutex;
				}
				if (mutex && !duplicate)
				{
					locks[index] = std::unique_lock<std::mutex>(*mutex);
				}
			}
			return locks;
		}

		VkResult WaitIdle(const VulkanDeviceState& state)
		{
			auto queueLocks = LockQueues(state);
			const auto result = state.Dispatch.vkDeviceWaitIdle(state.Device.device);
			ObserveVulkanResult(state, result, "vkDeviceWaitIdle");
			if (state.Diagnostics->IsLost())
			{
				state.LossRetirementAttempted = true;
				state.Diagnostics->RecordRetirement(result);
			}
			return result;
		}
	}

	void WaitForVulkanDeviceIdle(const VulkanDeviceState& state)
	{
		std::scoped_lock lock(state.RetirementMutex);
		if (!state.LossRetirementAttempted)
		{
			const auto result = WaitIdle(state);
			CheckVulkanResult(state, result, "vkDeviceWaitIdle");
			if (result != VK_SUCCESS)
			{
				throw std::runtime_error("Failed waiting for Vulkan device idle");
			}
		}
		RequireVulkanDevice(state);
	}

	void RetireLostVulkanDevice(const VulkanDeviceState& state) noexcept
	{
		if (!state.Diagnostics->IsLost() || state.Dispatch.vkDeviceWaitIdle == nullptr)
		{
			return;
		}
		try
		{
			std::scoped_lock lock(state.RetirementMutex);
			if (!state.LossRetirementAttempted)
			{
				const auto result = WaitIdle(state);
				if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST && state.Instance && state.Instance->Diagnostics.Log)
				{
					state.Instance->Diagnostics.Log->Record(Rhi::DiagnosticSeverity::Error, "DeviceLossRetirement",
						"Lost-device idle failed without retirement confirmation; inspect DeviceDiagnostics::Snapshot");
				}
			}
		}
		catch (...)
		{
			// Destruction must not replace the original device-loss exception.
			if (state.Instance && state.Instance->Diagnostics.Log)
			{
				state.Instance->Diagnostics.Log->Record(Rhi::DiagnosticSeverity::Error, "DeviceLossRetirement",
					"Lost-device retirement raised a host exception");
			}
		}
	}

} // namespace Swim::RhiVulkan
