#include "Tests/Fixtures/VulkanPipelineCacheCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.PipelineCache", "LazyDeviceCacheIsSharedAcrossPipelineOwnersAndDestroyedOnce")
{
	Testing::VulkanPipelineCacheCapture capture;
	SWIM_CHECK(capture.Device->GetPipelineCacheData().Status == Rhi::PipelineCacheDataStatus::Empty);
	SWIM_CHECK(capture.Device->LoadPipelineCache({}) == Rhi::PipelineCacheLoadStatus::Empty);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 0u);
	auto first = capture.MakePipeline();
	auto second = capture.MakePipeline();
	SWIM_REQUIRE(first && second);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
	SWIM_CHECK_EQUAL(capture.LastPipelineCache, capture.State->PipelineCache.Handle);
	SWIM_CHECK(capture.LastPipelineCache != VK_NULL_HANDLE);
	SWIM_CHECK_EQUAL(capture.Flags, 0u);
	SWIM_CHECK(capture.InitialData.empty());
	first.reset();
	second.reset();
	SWIM_CHECK_EQUAL(capture.CachesDestroyed, 0u);
	SWIM_CHECK(capture.Device->GetPipelineCacheData().Status == Rhi::PipelineCacheDataStatus::Ready);
	RhiVulkan::DestroyVulkanPipelineCache(*capture.State);
	RhiVulkan::DestroyVulkanPipelineCache(*capture.State);
	SWIM_CHECK_EQUAL(capture.CachesDestroyed, 1u);
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "ValidatedSeedReachesNativeCreationAndCannotReplaceAnActiveCache")
{
	Testing::VulkanPipelineCacheCapture capture;
	auto bytes = capture.EncodedData();
	SWIM_CHECK(capture.Device->LoadPipelineCache(bytes) == Rhi::PipelineCacheLoadStatus::Loaded);
	SWIM_CHECK(capture.InitialData == capture.Payload);
	bytes.assign(4, std::byte{99});
	SWIM_CHECK(capture.InitialData == capture.Payload);
	SWIM_REQUIRE(capture.MakePipeline());
	SWIM_CHECK(capture.Device->LoadPipelineCache(bytes) == Rhi::PipelineCacheLoadStatus::AlreadyInitialized);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
	const auto data = capture.Device->GetPipelineCacheData();
	SWIM_CHECK(data.Status == Rhi::PipelineCacheDataStatus::Ready);
	SWIM_CHECK(data.Bytes == capture.EncodedData());
	SWIM_CHECK_EQUAL(capture.SizeCalls, 1u);
	SWIM_CHECK_EQUAL(capture.DataCalls, 1u);
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "BadSeedDoesNotPreventColdCompilation")
{
	Testing::VulkanPipelineCacheCapture capture;
	const std::byte bad[]{ std::byte{1} };
	SWIM_CHECK(capture.Device->LoadPipelineCache(bad) == Rhi::PipelineCacheLoadStatus::InvalidData);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 0u);
	SWIM_REQUIRE(capture.MakePipeline());
	SWIM_CHECK(capture.InitialData.empty());
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "CacheAllocationFailureUsesUncachedCompilationWithoutRepeatedAttempts")
{
	Testing::VulkanPipelineCacheCapture capture;
	capture.CreateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_REQUIRE(capture.MakePipeline());
	SWIM_REQUIRE(capture.MakePipeline());
	SWIM_CHECK(capture.LastPipelineCache == VK_NULL_HANDLE);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
	SWIM_CHECK_EQUAL(capture.CachesDestroyed, 0u);
	SWIM_CHECK(capture.Device->GetPipelineCacheData().Status == Rhi::PipelineCacheDataStatus::Failed);
	SWIM_CHECK_EQUAL(capture.State->Instance->Diagnostics.Log->Snapshot().Warnings, 1u);
	SWIM_CHECK(!capture.State->Diagnostics->IsLost());
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "ExplicitSeedCreationFailureIsReportedWithoutDestroyingUndefinedOutput")
{
	Testing::VulkanPipelineCacheCapture capture;
	capture.CreateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	SWIM_CHECK(capture.Device->LoadPipelineCache(capture.EncodedData()) == Rhi::PipelineCacheLoadStatus::Failed);
	SWIM_CHECK_EQUAL(capture.CachesDestroyed, 0u);
	SWIM_REQUIRE(capture.MakePipeline());
	SWIM_CHECK(capture.LastPipelineCache == VK_NULL_HANDLE);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "ExportErrorsAndUntrustedSizesNeverReturnPartialPersistenceData")
{
	for (unsigned mode = 0; mode < 8; ++mode)
	{
		Testing::VulkanPipelineCacheCapture capture;
		SWIM_REQUIRE(capture.MakePipeline());
		auto expected = Rhi::PipelineCacheDataStatus::Failed;
		if (mode == 0)
		{
			capture.SizeResult = VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		if (mode == 1)
		{
			capture.ReportedSize = SIZE_MAX;
			expected = Rhi::PipelineCacheDataStatus::TooLarge;
		}
		if (mode == 2)
		{
			capture.ReportedSize = 0;
			expected = Rhi::PipelineCacheDataStatus::Empty;
		}
		if (mode == 3)
		{
			capture.DataResult = VK_INCOMPLETE;
			expected = Rhi::PipelineCacheDataStatus::Incomplete;
		}
		if (mode == 4)
		{
			capture.DataResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		}
		if (mode == 5)
		{
			capture.WrittenSize = capture.Payload.size() + 1;
		}
		if (mode == 6)
		{
			capture.WrittenSize = 31;
		}
		if (mode == 7)
		{
			capture.Payload[0] = std::byte{99};
		}
		const auto result = capture.Device->GetPipelineCacheData();
		SWIM_CHECK(result.Status == expected);
		SWIM_CHECK(result.Bytes.empty());
		SWIM_CHECK_EQUAL(capture.SizeCalls, 1u);
		SWIM_CHECK_EQUAL(capture.DataCalls, mode < 3 ? 0u : 1u);
	}
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "ActualWrittenSizeControlsSerialization")
{
	Testing::VulkanPipelineCacheCapture capture;
	SWIM_REQUIRE(capture.MakePipeline());
	capture.ReportedSize = capture.Payload.size() + 16;
	const auto result = capture.Device->GetPipelineCacheData();
	SWIM_CHECK(result.Status == Rhi::PipelineCacheDataStatus::Ready);
	SWIM_CHECK(result.Bytes == capture.EncodedData());
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "NativeLossAtCreationOrExportRaisesTypedErrorAndStopsRetries")
{
	for (unsigned mode = 0; mode < 3; ++mode)
	{
		Testing::VulkanPipelineCacheCapture capture;
		if (mode == 0)
		{
			capture.CreateResult = VK_ERROR_DEVICE_LOST;
			SWIM_CHECK_THROWS(capture.MakePipeline(), Rhi::DeviceLostError);
			SWIM_CHECK_EQUAL(capture.PipelinesCreated, 0u);
			SWIM_CHECK_EQUAL(capture.CachesDestroyed, 0u);
		}
		else
		{
			SWIM_REQUIRE(capture.MakePipeline());
			if (mode == 1)
			{
				capture.SizeResult = VK_ERROR_DEVICE_LOST;
			}
			else
			{
				capture.DataResult = VK_ERROR_DEVICE_LOST;
			}
			SWIM_CHECK_THROWS(capture.Device->GetPipelineCacheData(), Rhi::DeviceLostError);
		}
		const auto calls = capture.SizeCalls + capture.DataCalls;
		SWIM_CHECK_THROWS(capture.Device->GetPipelineCacheData(), Rhi::DeviceLostError);
		SWIM_CHECK_THROWS(capture.Device->LoadPipelineCache({}), Rhi::DeviceLostError);
		SWIM_CHECK_THROWS(capture.MakePipeline(), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.SizeCalls + capture.DataCalls, calls);
		SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
		SWIM_CHECK_EQUAL(capture.State->Instance->Diagnostics.Log->Snapshot().Errors, 1u);
	}
}
