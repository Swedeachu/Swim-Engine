#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSwapchainSession.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Sync/VulkanSemaphore.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <stdexcept>

using namespace Swim;

namespace
{

	struct SwapchainCapture;
	SwapchainCapture* active = nullptr;

	struct SwapchainCapture
	{
		std::shared_ptr<RhiVulkan::VulkanDeviceState> State = std::make_shared<RhiVulkan::VulkanDeviceState>();
		RhiVulkan::VulkanSwapchainSession Session{ State };
		RhiVulkan::VulkanSemaphore Signal{ State, RhiVulkan::FromNativeHandle<VkSemaphore>(7) };
		RhiVulkan::VulkanQueue Queue{ State, Rhi::QueueType::Graphics,
			RhiVulkan::FromNativeHandle<VkQueue>(8), 0, std::make_shared<std::mutex>() };
		VkResult AcquireResult = VK_SUCCESS;
		VkResult PresentResult = VK_SUCCESS;
		std::uint32_t NextImage = 1;
		std::uint32_t Acquires = 0;
		std::uint32_t Presents = 0;
		std::uint32_t PresentedImage = UINT32_MAX;
		std::uint64_t Timeout = UINT64_MAX;
		VkSwapchainKHR LastSwapchain = VK_NULL_HANDLE;
		VkSemaphore LastWait = VK_NULL_HANDLE;

		SwapchainCapture()
		{
			active = this;
			State->QueueFamilies.Graphics = 0;
			State->Dispatch.vkDestroySemaphore = +[](VkDevice, VkSemaphore, const VkAllocationCallbacks*) {};
			State->Dispatch.vkAcquireNextImageKHR = +[](VkDevice, VkSwapchainKHR swapchain,
				std::uint64_t timeout, VkSemaphore, VkFence, std::uint32_t* image) -> VkResult
			{
				++active->Acquires;
				active->Timeout = timeout;
				active->LastSwapchain = swapchain;
				// A failed acquire has no defined image output. Deliberately scribble
				// it to verify that failure never exposes an apparently usable image.
				*image = active->NextImage;
				return active->AcquireResult;
			};
			State->Dispatch.vkQueuePresentKHR = +[](VkQueue, const VkPresentInfoKHR* info) -> VkResult
			{
				++active->Presents;
				active->LastSwapchain = *info->pSwapchains;
				active->PresentedImage = *info->pImageIndices;
				active->LastWait = info->waitSemaphoreCount ? info->pWaitSemaphores[0] : VK_NULL_HANDLE;
				return active->PresentResult;
			};
		}

		void Build(std::uintptr_t handle = 9, std::uint32_t count = 3)
		{
			Session.SetImages(RhiVulkan::FromNativeHandle<VkSwapchainKHR>(handle), count);
		}

		bool Present(std::uint32_t index = 1)
		{
			std::array<Rhi::Semaphore*, 1> waits{ &Signal };
			return Session.Present(Queue, index, waits);
		}
	};

} // namespace

SWIM_TEST("RHI.Vulkan.Swapchain", "DormantMinimizedAndRestoredGenerations")
{
	SwapchainCapture capture;
	SWIM_CHECK(!Rhi::SwapchainAcquireResult{}.HasImage());
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).OutOfDate);
	capture.Session.Suspend();
	for (unsigned pump = 0; pump < 4; ++pump)
	{
		const auto result = capture.Session.Acquire(capture.Signal, nullptr);
		SWIM_CHECK(result.Suspended && !result.HasImage());
	}
	SWIM_CHECK_EQUAL(capture.Acquires, 0u);
	capture.Build();
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	SWIM_CHECK(capture.Present());
	capture.Session.Suspend();
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).Suspended);
	capture.Build(10, 2);
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	SWIM_CHECK(capture.Present());
	SWIM_CHECK_EQUAL(capture.LastSwapchain, RhiVulkan::FromNativeHandle<VkSwapchainKHR>(10));
	SWIM_CHECK_EQUAL(capture.PresentedImage, 1u);
	SWIM_CHECK_EQUAL(capture.LastWait, capture.Signal.GetSemaphore());
}

SWIM_TEST("RHI.Vulkan.Swapchain", "TimeoutDoesNotSignalAnImageOrRequireRebuild")
{
	SwapchainCapture capture;
	capture.Build();
	for (VkResult result : { VK_TIMEOUT, VK_NOT_READY })
	{
		capture.AcquireResult = result;
		const auto image = capture.Session.Acquire(capture.Signal, nullptr);
		SWIM_CHECK(image.NotReady && !image.HasImage() && !image.OutOfDate);
		SWIM_CHECK_EQUAL(image.ImageIndex, UINT32_MAX);
		SWIM_CHECK_THROWS(capture.Present(), std::invalid_argument);
	}
	SWIM_CHECK(capture.Timeout > 0 && capture.Timeout < UINT64_MAX);
	capture.AcquireResult = VK_SUCCESS;
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	SWIM_CHECK(capture.Present());
	SWIM_CHECK_EQUAL(capture.Presents, 1u);
}

SWIM_TEST("RHI.Vulkan.Swapchain", "SuboptimalAcquireMustBeConsumedBeforeReplacement")
{
	SwapchainCapture capture;
	capture.Build();
	capture.AcquireResult = VK_SUBOPTIMAL_KHR;
	const auto image = capture.Session.Acquire(capture.Signal, nullptr);
	SWIM_CHECK(image.HasImage() && image.Suboptimal);
	SWIM_CHECK_THROWS(capture.Build(10), std::logic_error);
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).OutOfDate);
	SWIM_CHECK_EQUAL(capture.Acquires, 1u);
	SWIM_CHECK(!capture.Present());
	SWIM_CHECK_THROWS(capture.Present(), std::invalid_argument);
	capture.Build(10);
	capture.AcquireResult = VK_SUCCESS;
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	SWIM_CHECK(capture.Present());
}

SWIM_TEST("RHI.Vulkan.Swapchain", "OutOfDateAcquireAndPresentBlockFurtherAcquisition")
{
	SwapchainCapture capture;
	capture.Build();
	capture.AcquireResult = VK_ERROR_OUT_OF_DATE_KHR;
	const auto result = capture.Session.Acquire(capture.Signal, nullptr);
	SWIM_CHECK(result.OutOfDate && !result.HasImage());
	SWIM_CHECK_EQUAL(result.ImageIndex, UINT32_MAX);
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).OutOfDate);
	SWIM_CHECK_EQUAL(capture.Acquires, 1u);
	capture.AcquireResult = VK_SUCCESS;
	for (VkResult status : { VK_ERROR_OUT_OF_DATE_KHR, VK_SUBOPTIMAL_KHR })
	{
		capture.Build();
		SWIM_REQUIRE(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
		capture.PresentResult = status;
		SWIM_CHECK(!capture.Present());
		SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).OutOfDate);
		SWIM_CHECK_THROWS(capture.Present(), std::invalid_argument);
	}
}

SWIM_TEST("RHI.Vulkan.Swapchain", "RetiredGenerationCannotAcquireUntilReplacementInstalled")
{
	SwapchainCapture capture;
	capture.Build();
	SWIM_REQUIRE(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	SWIM_CHECK_THROWS(capture.Session.RequireNoAcquiredImages(), std::logic_error);
	capture.Session.Suspend();
	SWIM_CHECK(!capture.Present());
	capture.Session.RequireNoAcquiredImages();
	capture.Session.SetImages(VK_NULL_HANDLE, 0);
	for (unsigned retry = 0; retry < 3; ++retry)
	{
		capture.Session.Invalidate();
		SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).OutOfDate);
	}
	SWIM_CHECK_EQUAL(capture.Acquires, 1u);
	capture.Build(11, 2);
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	SWIM_CHECK(capture.Present());
}

SWIM_TEST("RHI.Vulkan.Swapchain", "ForeignQueueInvalidIndexAndDuplicateWaitsRejectBeforeDispatch")
{
	SwapchainCapture capture;
	capture.Build();
	SWIM_CHECK_THROWS(capture.Present(0), std::invalid_argument);
	SWIM_REQUIRE(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	auto foreign = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanSemaphore foreignSignal{ foreign, VK_NULL_HANDLE };
	SWIM_CHECK_THROWS(capture.Session.Acquire(foreignSignal, nullptr), std::invalid_argument);
	RhiVulkan::VulkanQueue foreignQueue{ foreign, Rhi::QueueType::Graphics,
		RhiVulkan::FromNativeHandle<VkQueue>(10), 0, std::make_shared<std::mutex>() };
	SWIM_CHECK_THROWS(capture.Session.Present(foreignQueue, 1, {}), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Present(3), std::invalid_argument);
	std::array<Rhi::Semaphore*, 2> duplicates{ &capture.Signal, &capture.Signal };
	SWIM_CHECK_THROWS(capture.Session.Present(capture.Queue, 1, duplicates), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Presents, 0u);
	SWIM_CHECK(capture.Present());
}

SWIM_TEST("RHI.Vulkan.Swapchain", "FatalErrorsInvalidateAndRemainDistinctFromResizeSignals")
{
	SwapchainCapture capture;
	capture.Build();
	capture.AcquireResult = VK_ERROR_SURFACE_LOST_KHR;
	SWIM_CHECK_THROWS(capture.Session.Acquire(capture.Signal, nullptr), std::runtime_error);
	SWIM_CHECK(capture.Session.Acquire(capture.Signal, nullptr).OutOfDate);
	capture.Build();
	capture.AcquireResult = VK_SUCCESS;
	SWIM_REQUIRE(capture.Session.Acquire(capture.Signal, nullptr).HasImage());
	capture.PresentResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(capture.Present(), std::runtime_error);
	SWIM_CHECK_THROWS(capture.Present(), std::invalid_argument);
}
