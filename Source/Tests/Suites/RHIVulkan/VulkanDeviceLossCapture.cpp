#include "Tests/Fixtures/VulkanDeviceLossCapture.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanDeviceLossCapture* active = nullptr;
	}

	VulkanDeviceLossCapture::VulkanDeviceLossCapture()
	{
		active = this;
		State->Instance->Diagnostics.Echo = false;
		State->Instance->Diagnostics.Log = std::make_shared<Rhi::DiagnosticLog>();
		State->Dispatch.vkGetFenceStatus = +[](VkDevice, VkFence) -> VkResult
		{
			++active->HostCalls;
			return active->HostResult;
		};
		State->Dispatch.vkResetFences = +[](VkDevice, std::uint32_t, const VkFence*) -> VkResult
		{
			++active->HostCalls;
			return active->HostResult;
		};
		State->Dispatch.vkWaitForFences = +[](VkDevice, std::uint32_t, const VkFence*, VkBool32, std::uint64_t) -> VkResult
		{
			++active->HostCalls;
			++active->WaitCalls;
			return active->HostResult;
		};
		State->Dispatch.vkDestroyFence = +[](VkDevice, VkFence, const VkAllocationCallbacks*) {};
		State->Dispatch.vkGetSemaphoreCounterValue = +[](VkDevice, VkSemaphore, std::uint64_t* value) -> VkResult
		{
			++active->HostCalls;
			*value = 0;
			return active->HostResult;
		};
		State->Dispatch.vkWaitSemaphores = +[](VkDevice, const VkSemaphoreWaitInfo*, std::uint64_t) -> VkResult
		{
			++active->HostCalls;
			++active->WaitCalls;
			return active->HostResult;
		};
		State->Dispatch.vkDeviceWaitIdle = +[](VkDevice) -> VkResult
		{
			++active->HostCalls;
			return active->HostResult;
		};
		State->Dispatch.vkQueueWaitIdle = +[](VkQueue) -> VkResult
		{
			++active->HostCalls;
			return active->HostResult;
		};
		State->Dispatch.vkCreateSemaphore = +[](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* value) -> VkResult
		{
			*value = RhiVulkan::FromNativeHandle<VkSemaphore>(20);
			return VK_SUCCESS;
		};
		State->Dispatch.vkDestroySemaphore = +[](VkDevice, VkSemaphore, const VkAllocationCallbacks*) {};
		State->Dispatch.vkCreateCommandPool = +[](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* value) -> VkResult
		{
			*value = RhiVulkan::FromNativeHandle<VkCommandPool>(21);
			return VK_SUCCESS;
		};
		State->Dispatch.vkDestroyCommandPool = +[](VkDevice, VkCommandPool, const VkAllocationCallbacks*) { ++active->CommandPoolDestroys; };
		State->Dispatch.vkResetCommandPool = +[](VkDevice, VkCommandPool, VkCommandPoolResetFlags) -> VkResult
		{
			++active->HostCalls;
			return active->HostResult;
		};
		State->Dispatch.vkAllocateCommandBuffers = +[](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* value) -> VkResult
		{
			*value = RhiVulkan::FromNativeHandle<VkCommandBuffer>(22);
			return VK_SUCCESS;
		};
		State->QueueProperties = { { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 1, 64, {} } };
		State->Device.physical_device.properties.limits.timestampPeriod = 1.0f;
		State->Dispatch.vkCreateQueryPool = +[](VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool* value) -> VkResult
		{
			// Failed creation outputs are unspecified; deliberately write a handle.
			*value = RhiVulkan::FromNativeHandle<VkQueryPool>(99);
			return active->HostResult;
		};
		State->Dispatch.vkDestroyQueryPool = +[](VkDevice, VkQueryPool, const VkAllocationCallbacks*) { ++active->QueryDestroys; };
		auto mutex = std::make_shared<std::mutex>();
		State->QueueMutexes = { mutex, mutex, mutex };
		auto queue = std::make_unique<RhiVulkan::VulkanQueue>(State, Rhi::QueueType::Graphics,
			RhiVulkan::FromNativeHandle<VkQueue>(10), 0, mutex);
		Device = std::make_unique<RhiVulkan::VulkanDevice>(State, Rhi::AdapterInfo{}, std::move(queue), nullptr, nullptr);
	}

	VulkanDeviceLossCapture::~VulkanDeviceLossCapture()
	{
		RhiVulkan::RetireLostVulkanDevice(*State);
		Commands.reset();
		Device.reset();
	}

} // namespace Swim::Testing
