#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceLoss.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"

#include <cstdio>

namespace Swim::RhiVulkan
{

	VkPhysicalDeviceFaultFeaturesEXT QueryDeviceFaultFeatures(const volk::VolkInstanceTable& dispatch,
		VkPhysicalDevice physicalDevice, bool extensionAvailable, bool requested)
	{
		VkPhysicalDeviceFaultFeaturesEXT fault{};
		fault.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
		if (!extensionAvailable || !requested || dispatch.vkGetPhysicalDeviceFeatures2 == nullptr)
		{
			return fault;
		}
		VkPhysicalDeviceFeatures2 features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &fault;
		dispatch.vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
		// Binary vendor dumps are deliberately not enabled or fetched. The
		// portable report retains bounded textual and address/vendor diagnostics.
		fault.deviceFaultVendorBinary = VK_FALSE;
		return fault;
	}

	Rhi::DeviceFaultDetails CaptureVulkanDeviceFault(const VulkanDeviceState& state) noexcept
	{
		Rhi::DeviceFaultDetails report{};
		if (!state.Diagnostics->IsLost())
		{
			return report;
		}
		if (!state.DeviceFaultEnabled)
		{
			report.Status = Rhi::DeviceFaultStatus::Unsupported;
			return report;
		}
		report.Status = Rhi::DeviceFaultStatus::Failed;
		if (state.Dispatch.vkGetDeviceFaultInfoEXT == nullptr)
		{
			return report;
		}
		try
		{
			VkDeviceFaultCountsEXT counts{};
			counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
			auto result = state.Dispatch.vkGetDeviceFaultInfoEXT(state.Device.device, &counts, nullptr);
			report.NativeResult = result;
			if (result != VK_SUCCESS)
			{
				return report;
			}
			constexpr std::uint32_t maxEntries = 64;
			const bool truncated = counts.addressInfoCount > maxEntries || counts.vendorInfoCount > maxEntries || counts.vendorBinarySize != 0;
			std::vector<VkDeviceFaultAddressInfoEXT> addresses(std::min(counts.addressInfoCount, maxEntries));
			std::vector<VkDeviceFaultVendorInfoEXT> vendors(std::min(counts.vendorInfoCount, maxEntries));
			counts.addressInfoCount = static_cast<std::uint32_t>(addresses.size());
			counts.vendorInfoCount = static_cast<std::uint32_t>(vendors.size());
			counts.vendorBinarySize = 0;
			VkDeviceFaultInfoEXT info{};
			info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
			info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
			info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
			result = state.Dispatch.vkGetDeviceFaultInfoEXT(state.Device.device, &counts, &info);
			report.NativeResult = result;
			if (result != VK_SUCCESS && result != VK_INCOMPLETE)
			{
				return report;
			}
			report.Description.assign(info.description, std::find(std::begin(info.description), std::end(info.description), '\0'));
			char text[512]{};
			for (std::size_t index = 0; index < std::min(std::size_t(counts.addressInfoCount), addresses.size()); ++index)
			{
				const auto& address = addresses[index];
				std::snprintf(text, sizeof(text), "address type=%u reported=0x%llx precision=0x%llx",
					static_cast<unsigned>(address.addressType), static_cast<unsigned long long>(address.reportedAddress),
					static_cast<unsigned long long>(address.addressPrecision));
				report.Entries.emplace_back(text);
			}
			for (std::size_t index = 0; index < std::min(std::size_t(counts.vendorInfoCount), vendors.size()); ++index)
			{
				const auto& vendor = vendors[index];
				std::snprintf(text, sizeof(text), "vendor code=0x%llx data=0x%llx %.*s",
					static_cast<unsigned long long>(vendor.vendorFaultCode), static_cast<unsigned long long>(vendor.vendorFaultData),
					static_cast<int>(sizeof(vendor.description)), vendor.description);
				report.Entries.emplace_back(text);
			}
			report.Status = truncated || result == VK_INCOMPLETE || counts.addressInfoCount > addresses.size() || counts.vendorInfoCount > vendors.size()
				? Rhi::DeviceFaultStatus::Truncated : Rhi::DeviceFaultStatus::Complete;
		}
		catch (...)
		{
			// Diagnostic allocation failure must never hide the original device loss.
			report.Status = Rhi::DeviceFaultStatus::Failed;
		}
		return report;
	}

} // namespace Swim::RhiVulkan
