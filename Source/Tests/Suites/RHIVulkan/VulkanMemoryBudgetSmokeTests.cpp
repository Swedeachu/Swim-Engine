#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{

	void CheckMemorySnapshot(const Swim::Rhi::MemoryBudgetSnapshot& snapshot, bool driverSupported)
	{
		using namespace Swim;
		SWIM_REQUIRE(snapshot.IsAvailable());
		for (std::size_t index = 0; index < snapshot.Heaps.size(); ++index)
		{
			const auto& heap = snapshot.Heaps[index];
			SWIM_CHECK_EQUAL(heap.HeapIndex, index);
			SWIM_CHECK(heap.CapacityBytes > 0);
			SWIM_CHECK(heap.BudgetBytes > 0 && heap.BudgetBytes <= heap.CapacityBytes);
			// This smoke samples with allocation/free activity stopped.
			SWIM_CHECK(heap.BlockBytes >= heap.AllocationBytes);
			if (!driverSupported)
			{
				SWIM_CHECK(heap.Source == Rhi::MemoryBudgetSource::AllocatorEstimate);
				SWIM_CHECK_EQUAL(heap.UsageBytes, heap.BlockBytes);
			}
			std::fprintf(stderr, "[Swim memory] heap %u local=%u host-visible=%u source=%s capacity=%llu usage=%llu budget=%llu blocks=%u/%llu allocations=%u/%llu\n",
				heap.HeapIndex, static_cast<unsigned>(heap.DeviceLocal), static_cast<unsigned>(heap.HostVisible),
				heap.Source == Rhi::MemoryBudgetSource::DriverEstimate ? "driver" : "allocator",
				static_cast<unsigned long long>(heap.CapacityBytes), static_cast<unsigned long long>(heap.UsageBytes),
				static_cast<unsigned long long>(heap.BudgetBytes), heap.BlockCount, static_cast<unsigned long long>(heap.BlockBytes),
				heap.AllocationCount, static_cast<unsigned long long>(heap.AllocationBytes));
		}
	}

	void RunMemoryBudgetSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Memory budget smoke requires a working SDL desktop video driver");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE_MESSAGE(graphics, "Memory budget smoke requires the Swim Vulkan baseline and validation");
		SWIM_REQUIRE(graphics->IsValidationEnabled());
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const bool driverSupported = device->GetAdapterInfo().Capabilities.MemoryBudget;
		const auto baseline = device->GetMemoryBudgetSnapshot();
		CheckMemorySnapshot(baseline, driverSupported);
		for (unsigned cycle = 0; cycle < 4; ++cycle)
		{
			constexpr std::uint64_t bufferBytes = 4ull << 20;
			auto deviceBuffer = device->CreateBuffer({ bufferBytes, Rhi::BufferUsage::TransferDestination,
				Rhi::MemoryPreference::DeviceLocal, "memory telemetry device buffer" });
			auto upload = device->CreateBuffer({ bufferBytes, Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::CpuToGpu, "memory telemetry upload" });
			Rhi::TextureDesc textureDesc{};
			textureDesc.DebugName = "memory telemetry texture";
			textureDesc.Extent = { 256, 256, 1 };
			textureDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
			textureDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			auto texture = device->CreateTexture(textureDesc);
			SWIM_REQUIRE(deviceBuffer && upload && texture);
			const auto allocated = device->GetMemoryBudgetSnapshot();
			CheckMemorySnapshot(allocated, driverSupported);
			SWIM_REQUIRE_EQUAL(allocated.Heaps.size(), baseline.Heaps.size());
			std::uint64_t newAllocations = 0;
			std::uint64_t newBytes = 0;
			for (std::size_t index = 0; index < allocated.Heaps.size(); ++index)
			{
				const auto& heap = allocated.Heaps[index];
				SWIM_REQUIRE(heap.AllocationCount >= baseline.Heaps[index].AllocationCount);
				SWIM_REQUIRE(heap.AllocationBytes >= baseline.Heaps[index].AllocationBytes);
				newAllocations += heap.AllocationCount - baseline.Heaps[index].AllocationCount;
				newBytes += heap.AllocationBytes - baseline.Heaps[index].AllocationBytes;
			}
			SWIM_CHECK_EQUAL(newAllocations, 3u);
			SWIM_CHECK(newBytes >= bufferBytes * 2 + 256u * 256u * 4u);
			// Nothing was submitted: these allocations have no pending GPU uses.
			texture.reset();
			upload.reset();
			deviceBuffer.reset();
			const auto freed = device->GetMemoryBudgetSnapshot();
			CheckMemorySnapshot(freed, driverSupported);
			SWIM_REQUIRE_EQUAL(freed.Heaps.size(), baseline.Heaps.size());
			for (std::size_t index = 0; index < freed.Heaps.size(); ++index)
			{
				SWIM_CHECK_EQUAL(freed.Heaps[index].AllocationCount, baseline.Heaps[index].AllocationCount);
				SWIM_CHECK_EQUAL(freed.Heaps[index].AllocationBytes, baseline.Heaps[index].AllocationBytes);
			}
			// VMA may retain empty blocks; driver estimates can lag or change due
			// to other activity. Neither must return to the initial baseline.
		}
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "MemoryBudgetAllocationAndRelease", SWIM_TEST_LOCATION,
				+[] { Swim::Testing::RunValidatedVulkanSmoke(&RunMemoryBudgetSmoke); } });
		}
		return true;
	}();

} // namespace
