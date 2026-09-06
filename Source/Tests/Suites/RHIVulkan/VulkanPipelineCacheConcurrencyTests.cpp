#include "Tests/Fixtures/VulkanPipelineCacheCapture.h"
#include "Tests/Framework/Test.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <thread>

using namespace Swim;

namespace
{
	struct BuildGate
	{
		std::mutex Mutex;
		std::condition_variable Changed;
		unsigned Entered = 0;
		unsigned Active = 0;
		bool Release = false;
		bool TimedOut = false;
	};
	BuildGate* gate = nullptr;
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "ParallelBuildsShareCacheAndExportWaitsForHostUsers")
{
	Testing::VulkanPipelineCacheCapture capture;
	BuildGate state;
	gate = &state;
	capture.State->Dispatch.vkCreateGraphicsPipelines = +[](VkDevice, VkPipelineCache, std::uint32_t,
		const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline* pipeline) -> VkResult
	{
		std::unique_lock lock(gate->Mutex);
		++gate->Entered;
		++gate->Active;
		gate->Changed.notify_all();
		gate->TimedOut = !gate->Changed.wait_for(lock, std::chrono::seconds(5), [] { return gate->Release; }) || gate->TimedOut;
		--gate->Active;
		*pipeline = VK_NULL_HANDLE;
		return VK_SUCCESS;
	};
	std::atomic<unsigned> failures{ 0 };
	auto build = [&]()
	{
		try
		{
			VkGraphicsPipelineCreateInfo info{};
			VkPipeline pipeline = VK_NULL_HANDLE;
			if (RhiVulkan::CreateCachedVulkanGraphicsPipeline(*capture.State, info, pipeline) != VK_SUCCESS)
			{
				++failures;
			}
		}
		catch (...)
		{
			++failures;
		}
	};
	std::thread first(build);
	std::thread second(build);
	bool concurrent = false;
	{
		std::unique_lock lock(state.Mutex);
		concurrent = state.Changed.wait_for(lock, std::chrono::seconds(2), [&] { return state.Entered == 2; });
	}
	// An exclusive cache operation cannot enter while native builds hold shares.
	const bool exclusive = capture.State->PipelineCache.Mutex.try_lock();
	if (exclusive)
	{
		capture.State->PipelineCache.Mutex.unlock();
	}
	std::promise<void> exportStarted;
	auto started = exportStarted.get_future();
	auto exporter = std::async(std::launch::async, [&]()
	{
		exportStarted.set_value();
		return capture.Device->GetPipelineCacheData();
	});
	started.wait();
	{
		std::scoped_lock lock(state.Mutex);
		state.Release = true;
	}
	state.Changed.notify_all();
	first.join();
	second.join();
	const auto data = exporter.get();
	SWIM_CHECK(concurrent);
	SWIM_CHECK(!exclusive);
	SWIM_CHECK(!state.TimedOut);
	SWIM_CHECK_EQUAL(failures.load(), 0u);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
	SWIM_CHECK(data.Status == Rhi::PipelineCacheDataStatus::Ready);
	SWIM_CHECK_EQUAL(capture.SizeCalls, 1u);
	SWIM_CHECK_EQUAL(capture.DataCalls, 1u);
}

SWIM_TEST("RHI.Vulkan.PipelineCache", "LossObservedWhileWaitingForCacheLockPreventsNativeWork")
{
	Testing::VulkanPipelineCacheCapture capture;
	std::unique_lock lock(capture.State->PipelineCache.Mutex);
	std::promise<void> started;
	auto waiting = started.get_future();
	auto worker = std::async(std::launch::async, [&]()
	{
		started.set_value();
		try
		{
			capture.Device->LoadPipelineCache(capture.EncodedData());
			return false;
		}
		catch (const Rhi::DeviceLostError&)
		{
			return true;
		}
	});
	waiting.wait();
	RhiVulkan::ObserveVulkanResult(*capture.State, VK_ERROR_DEVICE_LOST, "another worker");
	lock.unlock();
	SWIM_CHECK(worker.get());
	SWIM_CHECK_EQUAL(capture.CachesCreated, 0u);
}
