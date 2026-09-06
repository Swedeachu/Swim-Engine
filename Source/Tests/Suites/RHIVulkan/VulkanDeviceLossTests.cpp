#include "Tests/Fixtures/VulkanDeviceLossCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.DeviceLoss", "NormalStatusesAndRecoverableFailuresDoNotMarkDeviceLost")
{
	Testing::VulkanDeviceLossCapture capture;
	for (auto result : { VK_SUCCESS, VK_TIMEOUT, VK_NOT_READY, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR,
		VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_OUT_OF_DEVICE_MEMORY })
	{
		SWIM_CHECK_EQUAL(RhiVulkan::CheckVulkanResult(*capture.State, result, "normal"), result);
	}
	RhiVulkan::VulkanFence fence(capture.State, VK_NULL_HANDLE);
	auto timeline = capture.Device->CreateTimeline(0);
	capture.HostResult = VK_NOT_READY;
	SWIM_CHECK(!fence.IsSignaled());
	capture.HostResult = VK_TIMEOUT;
	SWIM_CHECK(!fence.Wait(1));
	SWIM_CHECK(!timeline->Wait(1, 1));
	capture.HostResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	SWIM_CHECK_THROWS(fence.IsSignaled(), std::runtime_error);
	SWIM_CHECK_THROWS(fence.Wait(1), std::runtime_error);
	SWIM_CHECK_THROWS(timeline->Wait(1, 1), std::runtime_error);
	SWIM_CHECK_THROWS(capture.Device->WaitIdle(), std::runtime_error);
	SWIM_CHECK(!capture.State->Diagnostics->IsLost());
	SWIM_CHECK(capture.State->Instance->Diagnostics.Log->Snapshot().IsClean());
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "FenceTimelineQueueAndDeviceWaitsRaiseTypedLossAndStopNativeRetries")
{
	const char* operations[] = { "vkGetFenceStatus", "vkWaitForFences", "vkResetFences", "vkGetSemaphoreCounterValue",
		"vkWaitSemaphores", "vkDeviceWaitIdle", "vkQueueWaitIdle", "vkResetCommandPool" };
	for (unsigned operation = 0; operation < 8; ++operation)
	{
		Testing::VulkanDeviceLossCapture capture;
		RhiVulkan::VulkanFence fence(capture.State, VK_NULL_HANDLE);
		auto timeline = capture.Device->CreateTimeline(0);
		auto pool = capture.Device->CreateCommandPool(Rhi::QueueType::Graphics);
		capture.HostResult = VK_ERROR_DEVICE_LOST;
		auto run = [&]()
		{
			switch (operation)
			{
			case 0: fence.IsSignaled(); break;
			case 1: fence.Wait(1); break;
			case 2: fence.Reset(); break;
			case 3: timeline->GetCompletedValue(); break;
			case 4: timeline->Wait(1, 1); break;
			case 5: capture.Device->WaitIdle(); break;
			case 6: capture.Device->GetQueue(Rhi::QueueType::Graphics).WaitIdle(); break;
			case 7: pool->Reset(); break;
			}
		};
		SWIM_CHECK_THROWS(run(), Rhi::DeviceLostError);
		SWIM_CHECK_THROWS(run(), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.HostCalls, 1u);
		const auto report = capture.Device->GetDeviceDiagnostics()->Snapshot();
		SWIM_CHECK(report.Lost);
		SWIM_CHECK(report.Fault.Status == Rhi::DeviceFaultStatus::Unsupported);
		SWIM_CHECK_EQUAL(report.Operation, std::string(operations[operation]));
		SWIM_CHECK_EQUAL(capture.State->Instance->Diagnostics.Log->Snapshot().Errors, 1u);
	}
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "SubmitFailureLeavesFrameUncommittedAndBlocksFurtherRecording")
{
	Testing::VulkanDeviceLossCapture capture;
	auto frames = Rhi::FrameContextRing::Create(*capture.Device);
	SWIM_REQUIRE(frames);
	frames->BeginFrame();
	auto& commands = frames->CreateCommandList();
	commands.Begin();
	commands.End();
	capture.SubmitResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(frames->SubmitCurrent(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(frames->GetLastSubmittedPoint().Value, 0u);
	SWIM_CHECK_THROWS(frames->SubmitCurrent(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.SubmitCount, 1u);
	SWIM_CHECK_THROWS(capture.Commands->Begin(), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(capture.Device->CreateQueryPool({}), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(capture.Device->CreateBuffer({}), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(capture.Device->WaitIdle(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.State->Diagnostics->Snapshot().Operation, std::string("vkQueueSubmit2"));
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "LostFrameDrainRetiresDeviceOnceBeforeReleasingOwners")
{
	Testing::VulkanDeviceLossCapture capture;
	auto frames = Rhi::FrameContextRing::Create(*capture.Device);
	SWIM_REQUIRE(frames);
	frames->BeginFrame();
	frames->SubmitCurrent();
	capture.HostResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(frames->Drain(), Rhi::DeviceLostError);
	const auto calls = capture.HostCalls;
	auto diagnostics = capture.Device->GetDeviceDiagnostics();
	frames.reset();
	SWIM_CHECK_EQUAL(capture.HostCalls, calls + 1);
	SWIM_CHECK_EQUAL(capture.WaitCalls, 0u);
	SWIM_CHECK_EQUAL(capture.CommandPoolDestroys, 3u);
	capture.Device.reset();
	const auto report = diagnostics->Snapshot();
	SWIM_CHECK(report.Lost && report.RetirementAttempted);
	SWIM_CHECK_EQUAL(report.RetirementResult, VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_EQUAL(report.Operation, std::string("vkGetSemaphoreCounterValue"));
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "FailedCreationDiscardsUndefinedHandlesBeforeThrowing")
{
	{
		Testing::VulkanDeviceLossCapture capture;
		capture.HostResult = VK_ERROR_DEVICE_LOST;
		SWIM_CHECK_THROWS(capture.Device->CreateQueryPool({ Rhi::QueryType::Timestamp, 2, {} }), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.QueryDestroys, 0u);
	}
	{
		Testing::VulkanDeviceLossCapture capture;
		capture.State->Dispatch.vkCreateSampler = +[](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* value) -> VkResult
		{
			*value = RhiVulkan::FromNativeHandle<VkSampler>(999);
			return VK_ERROR_DEVICE_LOST;
		};
		SWIM_CHECK_THROWS(capture.Device->CreateSampler({}), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.SamplersDestroyed, 0u);
		SWIM_CHECK_EQUAL(capture.State->SamplerCount.load(), 0u);
	}
	{
		Testing::VulkanDeviceLossCapture capture;
		capture.State->Dispatch.vkCreateShaderModule = +[](VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* value) -> VkResult
		{
			*value = RhiVulkan::FromNativeHandle<VkShaderModule>(999);
			return VK_ERROR_DEVICE_LOST;
		};
		SWIM_CHECK_THROWS(capture.MakeProgram(), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.ModulesDestroyed, 0u);
	}
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "DescriptorAndPipelineFailuresRetireOnlySuccessfullyCreatedObjects")
{
	{
		Testing::VulkanDeviceLossCapture capture;
		auto program = capture.MakeProgram();
		capture.State->Dispatch.vkCreatePipelineLayout = +[](VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* value) -> VkResult
		{
			*value = RhiVulkan::FromNativeHandle<VkPipelineLayout>(999);
			return VK_ERROR_DEVICE_LOST;
		};
		SWIM_CHECK_THROWS(capture.Device->CreatePipelineLayout({ program.get(), {} }), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 0u);
	}
	{
		Testing::VulkanDeviceLossCapture capture;
		Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::Sampler, 1, Rhi::ShaderStageMask::Fragment } } };
		auto program = capture.MakeProgram({ { &schema, 1 }, {} });
		auto layout = capture.Device->CreatePipelineLayout({ program.get(), {} });
		SWIM_REQUIRE(layout);
		capture.AllocationResult = VK_ERROR_DEVICE_LOST;
		SWIM_CHECK_THROWS(capture.Device->CreateDescriptorTable({ layout.get(), 0, 0, {} }), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.PoolsDestroyed, 1u);
	}
	{
		Testing::VulkanDeviceLossCapture capture;
		capture.PipelineResult = VK_ERROR_DEVICE_LOST;
		SWIM_CHECK_THROWS(capture.MakePipeline(), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.PipelinesDestroyed, 1u);
	}
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "NonthrowingNameFailureStillRetainsLossAndStopsLaterWork")
{
	Testing::VulkanDeviceLossCapture capture;
	capture.State->Instance->Diagnostics.DebugUtilsEnabled = true;
	capture.State->Instance->Dispatch.vkSetDebugUtilsObjectNameEXT = +[](VkDevice, const VkDebugUtilsObjectNameInfoEXT*) -> VkResult
	{
		return VK_ERROR_DEVICE_LOST;
	};
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_DEVICE, 1, "device");
	SWIM_CHECK(capture.State->Diagnostics->IsLost());
	const auto log = capture.State->Instance->Diagnostics.Log->Snapshot();
	SWIM_CHECK_EQUAL(log.Errors, 1u);
	SWIM_CHECK_EQUAL(log.Warnings, 0u);
	SWIM_CHECK_THROWS(capture.Device->CreateTimeline(0), Rhi::DeviceLostError);
}

SWIM_TEST("RHI.Vulkan.DeviceLoss", "LostResourceCleanupUsesOneBarrierAndRetainsRetirementFailures")
{
	for (auto result : { VK_SUCCESS, VK_ERROR_DEVICE_LOST, VK_ERROR_OUT_OF_DEVICE_MEMORY })
	{
		Testing::VulkanDeviceLossCapture capture;
		auto first = capture.Device->CreateQueryPool({ Rhi::QueryType::Timestamp, 2, {} });
		auto second = capture.Device->CreateQueryPool({ Rhi::QueryType::Timestamp, 2, {} });
		SWIM_REQUIRE(first && second);
		RhiVulkan::ObserveVulkanResult(*capture.State, VK_ERROR_DEVICE_LOST, "injected loss");
		capture.HostResult = result;
		first.reset();
		second.reset();
		SWIM_CHECK_EQUAL(capture.HostCalls, 1u);
		SWIM_CHECK_EQUAL(capture.QueryDestroys, 2u);
		const auto report = capture.State->Diagnostics->Snapshot();
		SWIM_CHECK(report.RetirementAttempted);
		SWIM_CHECK_EQUAL(report.RetirementResult, result);
		SWIM_CHECK_EQUAL(report.Operation, std::string("injected loss"));
		SWIM_CHECK_THROWS(capture.Device->WaitIdle(), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.HostCalls, 1u);
		SWIM_CHECK_EQUAL(capture.State->Instance->Diagnostics.Log->Snapshot().Errors,
			result == VK_ERROR_OUT_OF_DEVICE_MEMORY ? 2u : 1u);
	}
}
