#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineCache.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineCacheData.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

#include <mutex>

namespace Swim::RhiVulkan
{

	namespace
	{
		bool Initialize(const VulkanDeviceState& state, std::span<const std::byte> data)
		{
			RequireVulkanDevice(state);
			auto& cache = state.PipelineCache;
			cache.InitializationAttempted = true;
			VkPipelineCacheCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
			// Keep Vulkan's internal cache synchronization enabled. Pipeline builds
			// can run together under shared host locks without an optional feature.
			info.initialDataSize = data.size();
			info.pInitialData = data.empty() ? nullptr : data.data();
			const auto result = state.Dispatch.vkCreatePipelineCache(state.Device.device, &info, nullptr, &cache.Handle);
			if (result != VK_SUCCESS)
			{
				// Output is unspecified on failure, including when the driver wrote it.
				cache.Handle = VK_NULL_HANDLE;
				CheckVulkanResult(state, result, "vkCreatePipelineCache");
				if (state.Instance && state.Instance->Diagnostics.Log)
				{
					state.Instance->Diagnostics.Log->Record(Rhi::DiagnosticSeverity::Warning, "PipelineCache",
						"Pipeline cache creation failed; this device will compile pipelines without a cache");
				}
				return false;
			}
			SetVulkanObjectName(state, VK_OBJECT_TYPE_PIPELINE_CACHE, ToNativeHandle(cache.Handle), "Swim pipeline cache");
			RequireVulkanDevice(state);
			return true;
		}

		void EnsureInitialized(const VulkanDeviceState& state)
		{
			{
				std::shared_lock lock(state.PipelineCache.Mutex);
				RequireVulkanDevice(state);
				if (state.PipelineCache.InitializationAttempted)
				{
					return;
				}
			}
			std::unique_lock lock(state.PipelineCache.Mutex);
			RequireVulkanDevice(state);
			if (!state.PipelineCache.InitializationAttempted)
			{
				Initialize(state, {});
			}
		}
	}

	Rhi::PipelineCacheLoadStatus LoadVulkanPipelineCache(const VulkanDeviceState& state, std::span<const std::byte> data)
	{
		RequireVulkanDevice(state);
		std::unique_lock lock(state.PipelineCache.Mutex);
		RequireVulkanDevice(state);
		if (state.PipelineCache.InitializationAttempted)
		{
			return Rhi::PipelineCacheLoadStatus::AlreadyInitialized;
		}
		std::span<const std::byte> nativeData;
		const auto status = DecodeVulkanPipelineCacheData(data, state.Device.physical_device.properties, nativeData);
		if (status != Rhi::PipelineCacheLoadStatus::Loaded)
		{
			return status;
		}
		return Initialize(state, nativeData) ? Rhi::PipelineCacheLoadStatus::Loaded : Rhi::PipelineCacheLoadStatus::Failed;
	}

	Rhi::PipelineCacheData ExportVulkanPipelineCache(const VulkanDeviceState& state)
	{
		RequireVulkanDevice(state);
		// Exclude pipeline builds for the complete size/data pair. This is host
		// synchronization only; it never drains queues or waits for GPU work.
		std::unique_lock lock(state.PipelineCache.Mutex);
		RequireVulkanDevice(state);
		const auto handle = state.PipelineCache.Handle;
		if (handle == VK_NULL_HANDLE)
		{
			return { state.PipelineCache.InitializationAttempted ? Rhi::PipelineCacheDataStatus::Failed : Rhi::PipelineCacheDataStatus::Empty, {} };
		}
		std::size_t size = 0;
		auto result = CheckVulkanResult(state, state.Dispatch.vkGetPipelineCacheData(state.Device.device, handle, &size, nullptr), "vkGetPipelineCacheData (size)");
		if (result != VK_SUCCESS)
		{
			return { result == VK_INCOMPLETE ? Rhi::PipelineCacheDataStatus::Incomplete : Rhi::PipelineCacheDataStatus::Failed, {} };
		}
		RequireVulkanDevice(state);
		if (size > Rhi::MaxPipelineCacheDataBytes - VulkanPipelineCacheEnvelopeBytes)
		{
			return { Rhi::PipelineCacheDataStatus::TooLarge, {} };
		}
		if (size == 0)
		{
			return { Rhi::PipelineCacheDataStatus::Empty, {} };
		}
		std::vector<std::byte> bytes(size);
		result = CheckVulkanResult(state, state.Dispatch.vkGetPipelineCacheData(state.Device.device, handle, &size, bytes.data()), "vkGetPipelineCacheData (data)");
		if (result != VK_SUCCESS || size > bytes.size())
		{
			return { result == VK_INCOMPLETE ? Rhi::PipelineCacheDataStatus::Incomplete : Rhi::PipelineCacheDataStatus::Failed, {} };
		}
		bytes.resize(size);
		RequireVulkanDevice(state);
		if (!IsCompatibleVulkanPipelineCacheHeader(bytes, state.Device.physical_device.properties))
		{
			return { Rhi::PipelineCacheDataStatus::Failed, {} };
		}
		return { Rhi::PipelineCacheDataStatus::Ready, EncodeVulkanPipelineCacheData(bytes, state.Device.physical_device.properties) };
	}

	VkResult CreateCachedVulkanGraphicsPipeline(const VulkanDeviceState& state, const VkGraphicsPipelineCreateInfo& info, VkPipeline& pipeline)
	{
		EnsureInitialized(state);
		std::shared_lock lock(state.PipelineCache.Mutex);
		RequireVulkanDevice(state);
		return CheckVulkanResult(state, state.Dispatch.vkCreateGraphicsPipelines(state.Device.device,
			state.PipelineCache.Handle, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines");
	}

	void DestroyVulkanPipelineCache(const VulkanDeviceState& state) noexcept
	{
		// Final device-state owner only: callers must have joined host workers.
		// A native cache is needed during pipeline creation, not GPU execution.
		if (state.PipelineCache.Handle != VK_NULL_HANDLE)
		{
			state.Dispatch.vkDestroyPipelineCache(state.Device.device, state.PipelineCache.Handle, nullptr);
			state.PipelineCache.Handle = VK_NULL_HANDLE;
		}
	}

} // namespace Swim::RhiVulkan
