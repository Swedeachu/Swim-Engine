#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanSwapchain.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanTimeline.h"

#include <stdexcept>

namespace Swim::RhiVulkan
{

	bool VulkanSwapchain::WaitForRetirement(const Rhi::TimelinePoint* safeAfter)
	{
		auto* timeline = safeAfter ? dynamic_cast<VulkanTimeline*>(safeAfter->Semaphore) : nullptr;
		if (timeline == nullptr || timeline->GetState()->DeviceState.get() != state.get())
		{
			throw std::invalid_argument("Swapchain replacement requires a same-device GPU retirement timeline");
		}
		if (timeline->GetCompletedValue() < safeAfter->Value && !timeline->Wait(safeAfter->Value, UINT64_MAX))
		{
			return false;
		}
		// Rendering completion alone cannot retire presentation waits. Retain the
		// existing core-WSI presentation-queue fallback; never idle the whole device.
		std::scoped_lock lock(*state->PresentationQueueMutex);
		return CheckVulkanResult(*state, state->Dispatch.vkQueueWaitIdle(state->PresentationQueue), "vkQueueWaitIdle") == VK_SUCCESS;
	}

} // namespace Swim::RhiVulkan
