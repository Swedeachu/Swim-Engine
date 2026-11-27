#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeferredReleases.h"

namespace Swim::RhiVulkan
{

	std::size_t VulkanDeferredReleases::Register(
		VkDevice deviceValue, PFN_vkGetSemaphoreCounterValue counter, PFN_vkDestroySemaphore destroy, VkSemaphore timeline, bool graphics)
	{
		std::scoped_lock lock(mutex);
		device = deviceValue;
		getCounter = counter;
		destroySemaphore = destroy;
		timelines.push_back(timeline);
		submitted.push_back(0);
		idleFloor.push_back(0);
		graphicsQueues.push_back(graphics);
		return timelines.size() - 1;
	}

	std::uint64_t VulkanDeferredReleases::Next(std::size_t queue)
	{
		std::scoped_lock lock(mutex);
		return ++submitted[queue];
	}

	void VulkanDeferredReleases::Idle(std::size_t queue) noexcept
	{
		{
			std::scoped_lock lock(mutex);

			if (queue >= submitted.size())
			{
				return;
			}

			// The floor covers the submission being recorded; the next real signal goes past it.
			idleFloor[queue] = ++submitted[queue];
		}

		Collect();
	}

	void VulkanDeferredReleases::IdleAll() noexcept
	{
		{
			std::scoped_lock lock(mutex);

			for (std::size_t queue = 0; queue < submitted.size(); ++queue)
			{
				idleFloor[queue] = ++submitted[queue];
			}
		}

		Collect();
	}

	std::vector<std::uint64_t> VulkanDeferredReleases::Needed()
	{
		auto needed = submitted;

		for (std::size_t queue = 0; queue < needed.size(); ++queue)
		{
			if (graphicsQueues[queue] && needed[queue] != 0)
			{
				needed[queue] += 1;
			}
		}

		return needed;
	}

	bool VulkanDeferredReleases::Passed(const std::vector<std::uint64_t>& needed)
	{
		for (std::size_t queue = 0; queue < needed.size() && queue < timelines.size(); ++queue)
		{
			if (needed[queue] == 0 || idleFloor[queue] >= needed[queue])
			{
				continue;
			}

			std::uint64_t completed = 0;

			// A failed query (a lost device) passes everything.
			if (getCounter(device, timelines[queue], &completed) == VK_SUCCESS && completed < needed[queue])
			{
				return false;
			}
		}

		return true;
	}

	void VulkanDeferredReleases::Release(std::function<void()> release) noexcept
	{
		try
		{
			{
				std::scoped_lock lock(mutex);

				if (!timelines.empty())
				{
					auto needed = Needed();

					if (!Passed(needed))
					{
						entries.push_back({ std::move(needed), std::move(release) });
						return;
					}
				}
			}

			release();
		}
		catch (...)
		{
			// Out of memory for the record: released at once, as before.
			if (release)
			{
				try
				{
					release();
				}
				catch (...)
				{
				}
			}
		}
	}

	void VulkanDeferredReleases::Collect() noexcept
	{
		std::vector<Entry> done;

		try
		{
			std::scoped_lock lock(mutex);

			for (auto entry = entries.begin(); entry != entries.end();)
			{
				if (Passed(entry->Needed))
				{
					done.push_back(std::move(*entry));
					entry = entries.erase(entry);
				}
				else
				{
					++entry;
				}
			}
		}
		catch (...)
		{
		}

		for (Entry& entry : done)
		{
			try
			{
				entry.Run();
			}
			catch (...)
			{
			}
		}
	}

	void VulkanDeferredReleases::Shutdown() noexcept
	{
		std::vector<Entry> done;
		std::vector<VkSemaphore> owned;

		{
			std::scoped_lock lock(mutex);
			done.swap(entries);
			owned.swap(timelines);
			submitted.clear();
			idleFloor.clear();
			graphicsQueues.clear();
		}

		for (Entry& entry : done)
		{
			try
			{
				entry.Run();
			}
			catch (...)
			{
			}
		}

		for (const VkSemaphore timeline : owned)
		{
			if (timeline != VK_NULL_HANDLE && destroySemaphore != nullptr)
			{
				destroySemaphore(device, timeline, nullptr);
			}
		}
	}

	std::size_t VulkanDeferredReleases::GetPending()
	{
		std::scoped_lock lock(mutex);
		return entries.size();
	}

} // namespace Swim::RhiVulkan
