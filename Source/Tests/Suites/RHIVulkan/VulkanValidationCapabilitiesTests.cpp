#include "Tests/Fixtures/VulkanValidationCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "UsesGlobalAndEnabledLayerProvidersOnly")
{
	Testing::VulkanValidationCapture capture;
	const auto caps = capture.Query();
	SWIM_REQUIRE(caps);
	SWIM_CHECK(caps->LayerAvailable && caps->DebugUtilsAvailable && caps->LayerSettingsAvailable);
	SWIM_CHECK_EQUAL(caps->LayerVersion, VK_MAKE_API_VERSION(0, 1, 4, 350));
	SWIM_CHECK(!capture.NonNullInstance && !capture.UnexpectedProvider);
	SWIM_CHECK(capture.Log.Snapshot().IsClean());
	capture.GlobalExtensions.push_back(capture.ValidationExtensions.front());
	capture.ValidationExtensions.clear();
	const auto global = capture.Query();
	SWIM_REQUIRE(global);
	SWIM_CHECK(global->LayerSettingsAvailable);
	capture.ValidationExtensions = capture.GlobalExtensions;
	capture.GlobalExtensions.clear();
	const auto layer = capture.Query();
	SWIM_REQUIRE(layer);
	SWIM_CHECK(!layer->DebugUtilsAvailable && layer->LayerDebugUtilsAvailable && layer->LayerSettingsAvailable);
}

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "AbsentValidationDoesNotBorrowUnrelatedLayerExtensions")
{
	Testing::VulkanValidationCapture capture;
	capture.Layers.erase(capture.Layers.begin());
	const auto caps = capture.Query();
	SWIM_REQUIRE(caps);
	SWIM_CHECK(!caps->LayerAvailable && !caps->LayerSettingsAvailable);
	SWIM_CHECK(caps->DebugUtilsAvailable);
	SWIM_CHECK_EQUAL(caps->LayerVersion, 0u);
	SWIM_CHECK_EQUAL(capture.CountCalls[2], 0u);
	SWIM_CHECK(!capture.UnexpectedProvider);
	capture.Layers.clear();
	capture.GlobalExtensions.clear();
	const auto empty = capture.Query();
	SWIM_REQUIRE(empty);
	SWIM_CHECK(!empty->LayerAvailable && !empty->DebugUtilsAvailable && !empty->LayerSettingsAvailable);
}

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "MissingLoaderFunctionsFailWithDiagnostics")
{
	Testing::VulkanValidationCapture capture;
	SWIM_CHECK(!RhiVulkan::QueryValidationCapabilities(nullptr, capture.Log));
	capture.MissingLayersFunction = true;
	SWIM_CHECK(!capture.Query());
	capture.MissingLayersFunction = false;
	capture.MissingExtensionsFunction = true;
	SWIM_CHECK(!capture.Query());
	SWIM_CHECK_EQUAL(capture.Log.Snapshot().Errors, 3u);
	SWIM_CHECK_EQUAL(capture.CountCalls[0], 0u);
}

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "IncompleteListsRetryBeforePublishingAnyCapabilities")
{
	for (unsigned stage = 0; stage < 3; ++stage)
	{
		Testing::VulkanValidationCapture capture;
		capture.IncompleteDataCalls[stage] = 2;
		const auto caps = capture.Query();
		SWIM_REQUIRE(caps);
		SWIM_CHECK(caps->LayerAvailable && caps->LayerSettingsAvailable);
		SWIM_CHECK_EQUAL(capture.DataCalls[stage], 3u);
		SWIM_CHECK_EQUAL(capture.CountCalls[stage], 3u);
		SWIM_CHECK(capture.Log.Snapshot().IsClean());
	}
}

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "ShrinkingListsTrimUnwrittenCapabilityEntries")
{
	Testing::VulkanValidationCapture capture;
	std::swap(capture.Layers[0], capture.Layers[1]);
	capture.Shrink[0] = true;
	const auto caps = capture.Query();
	SWIM_REQUIRE(caps);
	SWIM_CHECK(!caps->LayerAvailable);
	SWIM_CHECK_EQUAL(capture.CountCalls[2], 0u);
	capture.Shrink[0] = false;
	VkExtensionProperties unrelated{};
	std::strcpy(unrelated.extensionName, "VK_EXT_other");
	capture.ValidationExtensions.insert(capture.ValidationExtensions.begin(), unrelated);
	capture.Shrink[2] = true;
	const auto shrunk = capture.Query();
	SWIM_REQUIRE(shrunk);
	SWIM_CHECK(shrunk->LayerAvailable && !shrunk->LayerSettingsAvailable);
}

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "UnstableAndOversizedListsAreBounded")
{
	for (unsigned stage = 0; stage < 3; ++stage)
	{
		Testing::VulkanValidationCapture capture;
		capture.IncompleteDataCalls[stage] = 100;
		SWIM_CHECK(!capture.Query());
		SWIM_CHECK_EQUAL(capture.DataCalls[stage], 4u);
		capture.CountResult[stage] = VK_INCOMPLETE;
		SWIM_CHECK(!capture.Query());
		SWIM_CHECK_EQUAL(capture.CountCalls[stage], 8u);
		capture.CountResult[stage] = VK_SUCCESS;
		capture.CountOverride[stage] = 4097;
		SWIM_CHECK(!capture.Query());
		SWIM_CHECK_EQUAL(capture.DataCalls[stage], 4u);
		capture.CountOverride[stage] = 0;
		capture.IncompleteDataCalls[stage] = 0;
		capture.OversizedDataCount[stage] = true;
		SWIM_CHECK(!capture.Query());
		SWIM_CHECK_EQUAL(capture.Log.Snapshot().Errors, 4u);
	}
}

SWIM_TEST("RHI.Vulkan.ValidationCapabilities", "CountAndDataFailuresAreNotMissingSupport")
{
	for (unsigned stage = 0; stage < 3; ++stage)
	{
		Testing::VulkanValidationCapture capture;
		capture.CountResult[stage] = VK_ERROR_OUT_OF_HOST_MEMORY;
		SWIM_CHECK(!capture.Query());
		SWIM_CHECK_EQUAL(capture.DataCalls[stage], 0u);
		capture.CountResult[stage] = VK_SUCCESS;
		capture.DataResult[stage] = VK_ERROR_LAYER_NOT_PRESENT;
		SWIM_CHECK(!capture.Query());
		const auto snapshot = capture.Log.Snapshot();
		SWIM_CHECK_EQUAL(snapshot.Errors, 2u);
		SWIM_CHECK(snapshot.Messages.back().Text.find("result=-6") != std::string::npos);
	}
}
