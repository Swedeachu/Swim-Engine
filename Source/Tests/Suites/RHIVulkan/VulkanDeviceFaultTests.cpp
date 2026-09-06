#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <thread>

using namespace Swim;

namespace
{
	struct FaultCapture;
	FaultCapture* active = nullptr;

	struct FaultCapture
	{
		std::shared_ptr<RhiVulkan::VulkanDeviceState> State = std::make_shared<RhiVulkan::VulkanDeviceState>();
		std::uint32_t Calls = 0;
		std::uint32_t Addresses = 1;
		std::uint32_t Vendors = 1;
		std::uint32_t AddressCapacity = 0;
		std::uint32_t VendorCapacity = 0;
		VkResult CountsResult = VK_SUCCESS;
		VkResult DataResult = VK_SUCCESS;
		bool ObservedPending = false;
		bool BinaryDisabled = false;

		FaultCapture()
		{
			active = this;
			State->Instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
			State->Instance->Diagnostics.Echo = false;
			State->Instance->Diagnostics.Log = std::make_shared<Rhi::DiagnosticLog>();
			State->Diagnostics = std::make_shared<Rhi::DeviceDiagnostics>(true);
			State->DeviceFaultEnabled = true;
			State->Dispatch.vkGetDeviceFaultInfoEXT = +[](VkDevice, VkDeviceFaultCountsEXT* counts, VkDeviceFaultInfoEXT* info) -> VkResult
			{
				++active->Calls;
				// Reentrant snapshot proves the native call is outside the report lock.
				const auto report = active->State->Diagnostics->Snapshot();
				active->ObservedPending = report.Lost && report.Fault.Status == Rhi::DeviceFaultStatus::Pending;
				if (info == nullptr)
				{
					counts->addressInfoCount = active->Addresses;
					counts->vendorInfoCount = active->Vendors;
					counts->vendorBinarySize = 0;
					return active->CountsResult;
				}
				active->AddressCapacity = counts->addressInfoCount;
				active->VendorCapacity = counts->vendorInfoCount;
				active->BinaryDisabled = counts->vendorBinarySize == 0 && info->pVendorBinaryData == nullptr;
				std::memset(info->description, 'D', sizeof(info->description));
				for (std::uint32_t index = 0; index < counts->addressInfoCount; ++index)
				{
					info->pAddressInfos[index] = { VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT, 0x1234, 64 };
				}
				for (std::uint32_t index = 0; index < counts->vendorInfoCount; ++index)
				{
					auto& vendor = info->pVendorInfos[index];
					std::memset(vendor.description, 'V', sizeof(vendor.description));
					vendor.vendorFaultCode = 0x55;
					vendor.vendorFaultData = 0x66;
				}
				return active->DataResult;
			};
		}

		void Lose()
		{
			RhiVulkan::ObserveVulkanResult(*State, VK_ERROR_DEVICE_LOST, "vkQueueSubmit2");
		}
	};

	unsigned featureCalls = 0;
}

SWIM_TEST("RHI.Vulkan.DeviceFault", "ExtensionAndFeatureAreOptionalAndVendorBinariesAreNeverEnabled")
{
	volk::VolkInstanceTable dispatch{};
	dispatch.vkGetPhysicalDeviceFeatures2 = +[](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features)
	{
		++featureCalls;
		auto* fault = static_cast<VkPhysicalDeviceFaultFeaturesEXT*>(features->pNext);
		fault->deviceFault = VK_TRUE;
		fault->deviceFaultVendorBinary = VK_TRUE;
	};
	for (bool extension : { false, true })
	{
		for (bool requested : { false, true })
		{
			featureCalls = 0;
			const auto features = RhiVulkan::QueryDeviceFaultFeatures(dispatch, VK_NULL_HANDLE, extension, requested);
			SWIM_CHECK_EQUAL(featureCalls, extension && requested ? 1u : 0u);
			SWIM_CHECK_EQUAL(features.deviceFault, extension && requested ? VK_TRUE : VK_FALSE);
			SWIM_CHECK_EQUAL(features.deviceFaultVendorBinary, VK_FALSE);
			SWIM_CHECK_EQUAL(features.sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT);
			SWIM_CHECK(features.pNext == nullptr);
		}
	}
	dispatch.vkGetPhysicalDeviceFeatures2 = +[](VkPhysicalDevice, VkPhysicalDeviceFeatures2*) {};
	SWIM_CHECK(!RhiVulkan::QueryDeviceFaultFeatures(dispatch, VK_NULL_HANDLE, true, true).deviceFault);
	dispatch.vkGetPhysicalDeviceFeatures2 = nullptr;
	SWIM_CHECK(!RhiVulkan::QueryDeviceFaultFeatures(dispatch, VK_NULL_HANDLE, true, true).deviceFault);
}

SWIM_TEST("RHI.Vulkan.DeviceFault", "FirstLossCapturesOwnedDetailsOnceOutsideLocksAndSurvivesDeviceDestruction")
{
	std::shared_ptr<Rhi::DeviceDiagnostics> diagnostics;
	std::shared_ptr<Rhi::DiagnosticLog> log;
	{
		FaultCapture capture;
		diagnostics = capture.State->Diagnostics;
		log = capture.State->Instance->Diagnostics.Log;
		SWIM_CHECK(RhiVulkan::CaptureVulkanDeviceFault(*capture.State).Status == Rhi::DeviceFaultStatus::None);
		SWIM_CHECK_EQUAL(capture.Calls, 0u);
		std::string operation = "first operation";
		SWIM_CHECK_THROWS(RhiVulkan::CheckVulkanResult(*capture.State, VK_ERROR_DEVICE_LOST, operation), Rhi::DeviceLostError);
		operation.assign("overwritten");
		capture.Lose();
		SWIM_CHECK_EQUAL(capture.Calls, 2u);
		SWIM_CHECK(capture.ObservedPending && capture.BinaryDisabled);
	}
	const auto report = diagnostics->Snapshot();
	SWIM_CHECK(report.Lost && report.FaultReportingEnabled);
	SWIM_CHECK_EQUAL(report.Operation, std::string("first operation"));
	SWIM_CHECK_EQUAL(report.NativeResult, VK_ERROR_DEVICE_LOST);
	SWIM_CHECK(report.Fault.Status == Rhi::DeviceFaultStatus::Complete);
	SWIM_CHECK_EQUAL(report.Fault.Description, std::string(256, 'D'));
	SWIM_REQUIRE_EQUAL(report.Fault.Entries.size(), 2u);
	SWIM_CHECK(report.Fault.Entries[0].find("0x1234") != std::string::npos);
	SWIM_CHECK(report.Fault.Entries[1].find("code=0x55 data=0x66") != std::string::npos);
	SWIM_CHECK_EQUAL(log->Snapshot().Errors, 1u);
}

SWIM_TEST("RHI.Vulkan.DeviceFault", "UntrustedCountsAndIncompleteDataStayBoundedWithoutRetryLoops")
{
	FaultCapture capture;
	capture.Addresses = capture.Vendors = UINT32_MAX;
	capture.DataResult = VK_INCOMPLETE;
	capture.Lose();
	capture.Lose();
	const auto report = capture.State->Diagnostics->Snapshot();
	SWIM_CHECK(report.Fault.Status == Rhi::DeviceFaultStatus::Truncated);
	SWIM_CHECK_EQUAL(report.Fault.NativeResult, VK_INCOMPLETE);
	SWIM_CHECK_EQUAL(report.Fault.Entries.size(), 128u);
	SWIM_CHECK_EQUAL(capture.AddressCapacity, 64u);
	SWIM_CHECK_EQUAL(capture.VendorCapacity, 64u);
	SWIM_CHECK_EQUAL(capture.Calls, 2u);
}

SWIM_TEST("RHI.Vulkan.DeviceFault", "UnsupportedMissingDispatchAndCaptureErrorsPreserveOriginalLoss")
{
	for (unsigned mode = 0; mode < 4; ++mode)
	{
		FaultCapture capture;
		if (mode == 0)
		{
			capture.State->DeviceFaultEnabled = false;
		}
		if (mode == 1)
		{
			capture.State->Dispatch.vkGetDeviceFaultInfoEXT = nullptr;
		}
		if (mode == 2)
		{
			capture.CountsResult = VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		if (mode == 3)
		{
			capture.DataResult = VK_ERROR_UNKNOWN;
		}
		capture.Lose();
		const auto report = capture.State->Diagnostics->Snapshot();
		SWIM_CHECK(report.Lost);
		SWIM_CHECK_EQUAL(report.NativeResult, VK_ERROR_DEVICE_LOST);
		SWIM_CHECK(report.Fault.Status == (mode == 0 ? Rhi::DeviceFaultStatus::Unsupported : Rhi::DeviceFaultStatus::Failed));
		SWIM_CHECK(report.Fault.Entries.empty());
		SWIM_CHECK_EQUAL(capture.Calls, mode < 2 ? 0u : mode - 1);
	}
}

SWIM_TEST("RHI.Vulkan.DeviceFault", "ConcurrentFailuresProduceOneReportEvenWhenDiagnosticLogIsFull")
{
	FaultCapture capture;
	capture.State->Instance->Diagnostics.Log = std::make_shared<Rhi::DiagnosticLog>(0);
	std::vector<std::thread> workers;
	for (unsigned index = 0; index < 8; ++index)
	{
		workers.emplace_back([&capture]()
		{
			for (unsigned attempt = 0; attempt < 32; ++attempt)
			{
				capture.Lose();
			}
		});
	}
	for (auto& worker : workers)
	{
		worker.join();
	}
	SWIM_CHECK_EQUAL(capture.Calls, 2u);
	const auto report = capture.State->Diagnostics->Snapshot();
	SWIM_CHECK(report.Fault.Status == Rhi::DeviceFaultStatus::Complete);
	SWIM_CHECK_EQUAL(report.Operation, std::string("vkQueueSubmit2"));
	const auto log = capture.State->Instance->Diagnostics.Log->Snapshot();
	SWIM_CHECK_EQUAL(log.Errors, 1u);
	SWIM_CHECK(log.Dropped > 0 && !log.IsClean());
}
