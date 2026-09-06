#include "Tests/Fixtures/TemporaryPipelineCacheFile.h"
#include "Tests/Fixtures/VulkanPipelineCacheCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.PipelineCache", "ExportedFileSeedsAFreshDeviceAfterOriginalOwnersAreDestroyed")
{
	Testing::TemporaryPipelineCacheFile file;
	Rhi::PipelineCacheData retained;
	{
		Testing::VulkanPipelineCacheCapture cold;
		SWIM_REQUIRE(cold.MakePipeline());
		retained = cold.Device->GetPipelineCacheData();
		SWIM_REQUIRE(retained.Status == Rhi::PipelineCacheDataStatus::Ready);
	}
	file.Save(retained.Bytes);
	const auto bytes = file.Load();
	SWIM_CHECK(bytes == retained.Bytes);
	Testing::VulkanPipelineCacheCapture warm;
	SWIM_CHECK(warm.Device->LoadPipelineCache(bytes) == Rhi::PipelineCacheLoadStatus::Loaded);
	SWIM_CHECK(warm.InitialData == warm.Payload);
	SWIM_REQUIRE(warm.MakePipeline());
	SWIM_CHECK_EQUAL(warm.CachesCreated, 1u);
	SWIM_CHECK_EQUAL(warm.LastPipelineCache, warm.State->PipelineCache.Handle);
}
