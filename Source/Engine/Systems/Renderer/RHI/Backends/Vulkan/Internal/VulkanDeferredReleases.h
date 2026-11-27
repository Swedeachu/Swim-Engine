#pragma once

// Resources released while the GPU may still use them. Every queue with progress enabled
// signals a timeline semaphore of its own on each submission; a release records what each
// queue has submitted (plus the graphics queue's next submission, since the frame being
// recorded may still use the resource) and runs once the GPU has passed all of it. With
// nothing in flight, or no progress timelines (devices without a real queue), a release runs
// at once.

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace Swim::RhiVulkan
{

	class VulkanDeferredReleases
	{

	  public:
		VulkanDeferredReleases() = default;
		VulkanDeferredReleases(const VulkanDeferredReleases&) = delete;
		VulkanDeferredReleases& operator=(const VulkanDeferredReleases&) = delete;

		// A queue's progress timeline (created by the queue, destroyed by Shutdown).
		std::size_t Register(
			VkDevice device, PFN_vkGetSemaphoreCounterValue counter, PFN_vkDestroySemaphore destroy, VkSemaphore timeline, bool graphics);

		// The value the queue's next submission signals; call in submission order.
		std::uint64_t Next(std::size_t queue);

		// The queue (or every queue) went idle: everything submitted, and the submission being
		// recorded, has passed.
		void Idle(std::size_t queue) noexcept;
		void IdleAll() noexcept;

		// Runs release now when nothing it could touch is in flight, otherwise once it has passed.
		void Release(std::function<void()> release) noexcept;

		// Runs the releases the GPU has passed.
		void Collect() noexcept;

		// Runs every pending release (the device is idle or lost) and destroys the timelines.
		void Shutdown() noexcept;

		std::size_t GetPending();

	  private:
		struct Entry
		{
			std::vector<std::uint64_t> Needed;
			std::function<void()> Run;
		};

		std::vector<std::uint64_t> Needed();
		bool Passed(const std::vector<std::uint64_t>& needed);

		std::mutex mutex;
		VkDevice device = VK_NULL_HANDLE;
		PFN_vkGetSemaphoreCounterValue getCounter = nullptr;
		PFN_vkDestroySemaphore destroySemaphore = nullptr;
		std::vector<VkSemaphore> timelines;
		std::vector<std::uint64_t> submitted;
		std::vector<std::uint64_t> idleFloor; // Values passed by an idle wait, never signalled.
		std::vector<bool> graphicsQueues;
		std::vector<Entry> entries;
	};

} // namespace Swim::RhiVulkan
