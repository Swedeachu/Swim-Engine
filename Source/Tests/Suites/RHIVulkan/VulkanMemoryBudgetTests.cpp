#include "Tests/Fixtures/VulkanMemoryBudgetCapture.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <limits>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.MemoryBudget", "RealAllocatorCountersTrackAllocationFreeAndSeparateHeapTypes")
{
	Testing::VulkanMemoryBudgetCapture capture;
	const auto baseline = capture.Device->GetMemoryBudgetSnapshot();
	SWIM_REQUIRE_EQUAL(baseline.Heaps.size(), 2u);
	SWIM_CHECK(baseline.Heaps[0].DeviceLocal && baseline.Heaps[0].HostVisible);
	SWIM_CHECK(!baseline.Heaps[1].DeviceLocal && baseline.Heaps[1].HostVisible);
	SWIM_CHECK_EQUAL(baseline.Heaps[0].AllocationCount, 0u);
	auto deviceAllocation = capture.Allocate(4096);
	auto hostAllocation = capture.Allocate(8192, 1);
	const auto allocated = capture.Device->GetMemoryBudgetSnapshot();
	for (unsigned index = 0; index < 2; ++index)
	{
		const auto& heap = allocated.Heaps[index];
		SWIM_CHECK_EQUAL(heap.HeapIndex, index);
		SWIM_CHECK_EQUAL(heap.CapacityBytes, capture.Properties.memoryHeaps[index].size);
		SWIM_CHECK_EQUAL(heap.AllocationCount, 1u);
		SWIM_CHECK_EQUAL(heap.BlockCount, 1u);
		SWIM_CHECK_EQUAL(heap.AllocationBytes, index == 0 ? 4096u : 8192u);
		SWIM_CHECK_EQUAL(heap.BlockBytes, heap.AllocationBytes);
		SWIM_CHECK(heap.Source == Rhi::MemoryBudgetSource::DriverEstimate);
		SWIM_CHECK_EQUAL(heap.UsageBytes, capture.Driver.heapUsage[index]);
		SWIM_CHECK_EQUAL(heap.BudgetBytes, capture.Driver.heapBudget[index]);
	}
	capture.Free(deviceAllocation);
	capture.Free(hostAllocation);
	const auto freed = capture.Device->GetMemoryBudgetSnapshot();
	SWIM_CHECK_EQUAL(freed.Heaps[0].AllocationBytes, 0u);
	SWIM_CHECK_EQUAL(freed.Heaps[1].AllocationBytes, 0u);
	SWIM_CHECK_EQUAL(freed.Heaps[0].BlockBytes, 0u);
	SWIM_CHECK_EQUAL(capture.NativeAllocations.load(), capture.NativeFrees.load());
	SWIM_CHECK_EQUAL(capture.IdleCalls.load(), 0u);
}

SWIM_TEST("RHI.Vulkan.MemoryBudget", "DriverEstimatesRefreshWhileAllocatorIsIdleAndSnapshotsOwnValues")
{
	Rhi::MemoryBudgetSnapshot retained;
	{
		Testing::VulkanMemoryBudgetCapture capture;
		retained = capture.Device->GetMemoryBudgetSnapshot();
		const auto calls = capture.DriverCalls.load();
		capture.Driver.heapBudget[0] = 100;
		capture.Driver.heapUsage[0] = 150;
		const auto current = capture.Device->GetMemoryBudgetSnapshot();
		SWIM_CHECK_EQUAL(capture.DriverCalls.load(), calls + 1);
		SWIM_CHECK_EQUAL(current.Heaps[0].BudgetBytes, 100u);
		SWIM_CHECK_EQUAL(current.Heaps[0].UsageBytes, 150u);
		SWIM_CHECK(current.Heaps[0].IsOverBudget());
		SWIM_CHECK_EQUAL(current.Heaps[0].GetHeadroomBytes(), 0u);
		SWIM_CHECK_EQUAL(current.Heaps[0].AllocationBytes, 0u);
		SWIM_CHECK_EQUAL(capture.IdleCalls.load(), 0u);
	}
	SWIM_REQUIRE(retained.IsAvailable());
	SWIM_CHECK_EQUAL(retained.Heaps[0].BudgetBytes, 512ull << 20);
	SWIM_CHECK_EQUAL(retained.Heaps[0].UsageBytes, 16ull << 20);
}

SWIM_TEST("RHI.Vulkan.MemoryBudget", "NoExtensionStillReportsRealAllocatorBlocksAndExplicitFallback")
{
	Testing::VulkanMemoryBudgetCapture capture(false);
	capture.Allocate(4096, 0, false);
	capture.Allocate(8192, 0, false);
	const auto snapshot = capture.Device->GetMemoryBudgetSnapshot();
	SWIM_REQUIRE_EQUAL(snapshot.Heaps.size(), 2u);
	const auto& heap = snapshot.Heaps[0];
	SWIM_CHECK(heap.Source == Rhi::MemoryBudgetSource::AllocatorEstimate);
	SWIM_CHECK_EQUAL(heap.AllocationCount, 2u);
	SWIM_CHECK_EQUAL(heap.AllocationBytes, 12288u);
	SWIM_CHECK_EQUAL(heap.BlockCount, 1u);
	SWIM_CHECK(heap.BlockBytes > heap.AllocationBytes);
	SWIM_CHECK_EQUAL(heap.UsageBytes, heap.BlockBytes);
	SWIM_CHECK_EQUAL(heap.BudgetBytes, heap.CapacityBytes * 8 / 10);
	SWIM_CHECK_EQUAL(capture.DriverCalls.load(), 0u);
	SWIM_CHECK_EQUAL(capture.IdleCalls.load(), 0u);
}

SWIM_TEST("RHI.Vulkan.MemoryBudget", "InvalidDriverBudgetsFallBackPerHeapWithoutTrustingCachedEstimates")
{
	Testing::VulkanMemoryBudgetCapture capture;
	capture.Allocate(4096);
	for (auto invalid : { 0ull, (1ull << 30) + 1 })
	{
		capture.Driver.heapBudget[0] = invalid;
		capture.Driver.heapUsage[0] = UINT64_MAX;
		const auto snapshot = capture.Device->GetMemoryBudgetSnapshot();
		SWIM_CHECK(snapshot.Heaps[0].Source == Rhi::MemoryBudgetSource::AllocatorEstimate);
		SWIM_CHECK_EQUAL(snapshot.Heaps[0].UsageBytes, 4096u);
		SWIM_CHECK_EQUAL(snapshot.Heaps[0].BudgetBytes, (1ull << 30) * 8 / 10);
		SWIM_CHECK(snapshot.Heaps[1].Source == Rhi::MemoryBudgetSource::DriverEstimate);
	}
	capture.State->Instance->Dispatch.vkGetPhysicalDeviceMemoryProperties2 = nullptr;
	const auto calls = capture.DriverCalls.load();
	const auto fallback = capture.Device->GetMemoryBudgetSnapshot();
	SWIM_CHECK(fallback.Heaps[1].Source == Rhi::MemoryBudgetSource::AllocatorEstimate);
	SWIM_CHECK_EQUAL(capture.DriverCalls.load(), calls);
}

SWIM_TEST("RHI.Vulkan.MemoryBudget", "KnownOrConcurrentLossRejectsTelemetryWithoutRetriesOrIdle")
{
	Testing::VulkanMemoryBudgetCapture capture;
	capture.LoseDuringQuery = true;
	SWIM_CHECK_THROWS(capture.Device->GetMemoryBudgetSnapshot(), Rhi::DeviceLostError);
	const auto calls = capture.DriverCalls.load();
	SWIM_CHECK_THROWS(capture.Device->GetMemoryBudgetSnapshot(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.DriverCalls.load(), calls);
	SWIM_CHECK_EQUAL(capture.IdleCalls.load(), 0u);
	SWIM_CHECK_EQUAL(capture.State->Diagnostics->Snapshot().Operation, std::string("concurrent loss"));
}

SWIM_TEST("RHI.Vulkan.MemoryBudget", "MissingAllocatorIsUnavailableAndDoesNotHideKnownLoss")
{
	auto state = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanDevice device(state, {}, nullptr, nullptr, nullptr);
	SWIM_CHECK(!device.GetMemoryBudgetSnapshot().IsAvailable());
	RhiVulkan::ObserveVulkanResult(*state, VK_ERROR_DEVICE_LOST, "lost before sampling");
	SWIM_CHECK_THROWS(device.GetMemoryBudgetSnapshot(), Rhi::DeviceLostError);
}

SWIM_TEST("RHI.Vulkan.MemoryBudget", "HeapConversionIsBoundedAndFallbackArithmeticCannotOverflow")
{
	VkPhysicalDeviceMemoryProperties properties{};
	properties.memoryHeapCount = VK_MAX_MEMORY_HEAPS;
	properties.memoryTypeCount = VK_MAX_MEMORY_TYPES;
	std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
	for (std::uint32_t index = 0; index < VK_MAX_MEMORY_HEAPS; ++index)
	{
		properties.memoryHeaps[index].size = UINT64_MAX;
		budgets[index].statistics.blockBytes = UINT64_MAX;
	}
	for (std::uint32_t index = 0; index < VK_MAX_MEMORY_TYPES; ++index)
	{
		properties.memoryTypes[index] = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, index % VK_MAX_MEMORY_HEAPS };
	}
	const auto snapshot = RhiVulkan::BuildVulkanMemoryBudgetSnapshot(properties, budgets, nullptr);
	SWIM_REQUIRE_EQUAL(snapshot.Heaps.size(), std::size_t(VK_MAX_MEMORY_HEAPS));
	for (const auto& heap : snapshot.Heaps)
	{
		SWIM_CHECK(heap.HostVisible);
		SWIM_CHECK_EQUAL(heap.BudgetBytes, 14757395258967641292ull);
		SWIM_CHECK_EQUAL(heap.GetHeadroomBytes(), 0u);
		SWIM_CHECK(heap.IsOverBudget());
	}
	SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanMemoryBudgetSnapshot(properties, {}, nullptr), std::runtime_error);
	properties.memoryHeapCount = VK_MAX_MEMORY_HEAPS + 1;
	SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanMemoryBudgetSnapshot(properties, budgets, nullptr), std::runtime_error);
	properties.memoryHeapCount = 0;
	SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanMemoryBudgetSnapshot(properties, budgets, nullptr), std::runtime_error);
	properties.memoryHeapCount = 1;
	properties.memoryTypeCount = VK_MAX_MEMORY_TYPES + 1;
	SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanMemoryBudgetSnapshot(properties, budgets, nullptr), std::runtime_error);
	properties.memoryTypeCount = 1;
	properties.memoryTypes[0].heapIndex = 1;
	SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanMemoryBudgetSnapshot(properties, budgets, nullptr), std::runtime_error);
}
