#pragma once

// Shared Vulkan instance/device bootstrap state. Every backend object type
// (queues, resources, sync primitives, swapchain) holds a shared_ptr to a
// VulkanDeviceState so it can reach the loaded dispatch table and the VMA
// allocator without depending on the concrete VulkanDevice type.

#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"

#include <volk.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeferredReleases.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceLoss.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineCache.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSubmissionWorker.h"

#include <array>
#include <map>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Swim::RhiVulkan
{

	struct QueueFamilySelection
	{
		std::uint32_t Graphics = UINT32_MAX;
		std::uint32_t Compute = UINT32_MAX;
		std::uint32_t Transfer = UINT32_MAX;

		bool IsValid() const
		{
			return Graphics != UINT32_MAX && Compute != UINT32_MAX && Transfer != UINT32_MAX;
		}
	};

	struct VulkanInstanceState
	{
		VulkanDiagnosticsState Diagnostics;
		vkb::Instance Instance{};
		volk::VolkInstanceTable Dispatch{};
		bool LoaderAcquired = false;
		bool RequestDeviceFaultDiagnostics = true;
		bool SwapchainColorSpaceEnabled = false;

		~VulkanInstanceState()
		{
			if (Instance.instance != VK_NULL_HANDLE)
			{
				vkb::destroy_instance(Instance);
			}

			if (LoaderAcquired)
			{
				Platform::Internal::ReleaseVulkanLoader();
			}
		}
	};

	struct VulkanDeviceState
	{
		std::shared_ptr<VulkanInstanceState> Instance;
		std::shared_ptr<Rhi::DeviceDiagnostics> Diagnostics = std::make_shared<Rhi::DeviceDiagnostics>();
		bool DeviceFaultEnabled = false;
		bool MemoryBudgetEnabled = false;
		// SWIM_PIPELINE_STATS=1: pipelines capture their executable statistics, logged on creation.
		bool PipelineStatisticsEnabled = false;
		// Optional descriptorBindingUpdateUnusedWhilePending was enabled with the
		// required descriptor-indexing features (GraphicsCapabilities::BindlessDescriptors).
		bool BindlessDescriptorsEnabled = false;
		// Update-after-bind limits for layouts containing bindless spaces.
		VkPhysicalDeviceDescriptorIndexingProperties DescriptorIndexing{};
		mutable VulkanPipelineCacheState PipelineCache;
		std::array<std::shared_ptr<std::mutex>, 3> QueueMutexes{};
		mutable std::mutex RetirementMutex;
		mutable bool LossRetirementAttempted = false;
		vkb::Device Device{};
		volk::VolkDeviceTable Dispatch{};
		QueueFamilySelection QueueFamilies{};
		std::vector<VkQueueFamilyProperties> QueueProperties;
		VkQueue PresentationQueue = VK_NULL_HANDLE;
		std::shared_ptr<std::mutex> PresentationQueueMutex;
		// Set: every submission and present runs on this thread, in order (Device::SetSubmissionThread).
		std::unique_ptr<VulkanSubmissionWorker> SubmitWorker;

		// Waits for the submission thread's queued work (not from that thread).
		void DrainSubmissions() const
		{
			if (SubmitWorker && !SubmitWorker->IsWorkerThread())
			{
				SubmitWorker->Drain();
			}
		}
		VmaVulkanFunctions AllocatorFunctions{};
		VmaAllocator Allocator = nullptr;
		// Buffers, images and views released while the GPU may still use them run their release
		// once the queues' progress timelines pass it (Shutdown runs the rest, before the allocator).
		std::unique_ptr<VulkanDeferredReleases> Deferred = std::make_unique<VulkanDeferredReleases>();
		std::atomic<std::uint32_t> SamplerCount{ 0 };
		// Reset, reusable one-set descriptor pools by their creation signature (flags, then
		// type/count pairs): descriptor tables created every frame reuse them instead of a
		// vkCreateDescriptorPool/vkDestroyDescriptorPool pair each.
		mutable std::mutex DescriptorPoolMutex;
		// Counts destroyed pipeline layouts (Device::GetPipelineLayoutGeneration).
		std::atomic<std::uint64_t> PipelineLayoutGeneration{ 0 };
		mutable std::map<std::vector<std::uint32_t>, std::vector<VkDescriptorPool>> FreeDescriptorPools;

		~VulkanDeviceState()
		{
			SubmitWorker.reset();
			RetireLostVulkanDevice(*this);

			if (Deferred->GetPending() != 0 && !Diagnostics->IsLost() && Device.device != VK_NULL_HANDLE &&
				Dispatch.vkDeviceWaitIdle != nullptr)
			{
				Dispatch.vkDeviceWaitIdle(Device.device);
			}

			Deferred->Shutdown();

			if (Device.device != VK_NULL_HANDLE && Dispatch.vkDestroyDescriptorPool != nullptr)
			{
				for (auto& [signature, pools] : FreeDescriptorPools)
				{
					(void)signature;

					for (const VkDescriptorPool pool : pools)
					{
						Dispatch.vkDestroyDescriptorPool(Device.device, pool, nullptr);
					}
				}
			}

			FreeDescriptorPools.clear();
			DestroyVulkanPipelineCache(*this);

			if (Allocator != nullptr)
			{
				vmaDestroyAllocator(Allocator);
			}

			if (Device.device != VK_NULL_HANDLE)
			{
				vkb::destroy_device(Device);
			}
		}
	};

} // namespace Swim::RhiVulkan
