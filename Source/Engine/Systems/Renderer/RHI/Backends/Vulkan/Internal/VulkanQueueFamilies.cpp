#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanQueueFamilies.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

namespace Swim::RhiVulkan
{

		QueueFamilySelection SelectQueueFamilies(
			VkInstance instance,
			const vkb::PhysicalDevice& physicalDevice)
		{
			QueueFamilySelection selection{};
			const auto families = physicalDevice.get_queue_families();

			for (std::uint32_t index = 0; index < families.size(); ++index)
			{
				const auto& family = families[index];
				if (family.queueCount == 0 || (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				{
					continue;
				}

				if (Platform::Internal::GetVulkanPresentationSupport(
					ToNativeHandle(instance), ToNativeHandle(physicalDevice.physical_device), index))
				{
					selection.Graphics = index;
					break;
				}
			}

			for (std::uint32_t index = 0; index < families.size(); ++index)
			{
				const auto& family = families[index];
				if (family.queueCount == 0 || (family.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
				{
					continue;
				}

				if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				{
					selection.Compute = index;
					break;
				}
			}
			if (selection.Compute == UINT32_MAX)
			{
				selection.Compute = selection.Graphics;
			}

			for (std::uint32_t index = 0; index < families.size(); ++index)
			{
				const auto& family = families[index];
				if (family.queueCount == 0 || (family.queueFlags & VK_QUEUE_TRANSFER_BIT) == 0)
				{
					continue;
				}

				if ((family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
				{
					selection.Transfer = index;
					break;
				}
			}
			if (selection.Transfer == UINT32_MAX)
			{
				for (std::uint32_t index = 0; index < families.size(); ++index)
				{
					const auto& family = families[index];
					if (family.queueCount > 0 &&
						(family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 &&
						(family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
					{
						selection.Transfer = index;
						break;
					}
				}
			}
			if (selection.Transfer == UINT32_MAX)
			{
				selection.Transfer = selection.Compute != UINT32_MAX ? selection.Compute : selection.Graphics;
			}

			return selection;
		}

} // namespace Swim::RhiVulkan
