#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <vk_mem_alloc.h>
#include <volk.h>
#include <VkBootstrap.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace Swim::RhiVulkan
{

	namespace
	{

		Rhi::AdapterInfo BuildAdapterInfo(
			const volk::VolkInstanceTable& dispatch,
			const vkb::PhysicalDevice& physicalDevice)
		{
			VkPhysicalDeviceVulkan12Features features12{};
			features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

			VkPhysicalDeviceVulkan13Features features13{};
			features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

			VkPhysicalDeviceFeatures2 features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			features.pNext = &features12;
			features12.pNext = &features13;

			void** featureTail = &features13.pNext;

#ifdef VK_EXT_descriptor_buffer
			VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures{};
			descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
			*featureTail = &descriptorBufferFeatures;
			featureTail = &descriptorBufferFeatures.pNext;
#endif

#ifdef VK_EXT_mesh_shader
			VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
			meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
			*featureTail = &meshShaderFeatures;
			featureTail = &meshShaderFeatures.pNext;
#endif

#ifdef VK_KHR_ray_query
			VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
			rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
			*featureTail = &rayQueryFeatures;
			featureTail = &rayQueryFeatures.pNext;
#endif

#ifdef VK_KHR_ray_tracing_pipeline
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
			rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			*featureTail = &rayTracingPipelineFeatures;
			featureTail = &rayTracingPipelineFeatures.pNext;
#endif

			*featureTail = nullptr;
			dispatch.vkGetPhysicalDeviceFeatures2(physicalDevice.physical_device, &features);

			VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties{};
			descriptorIndexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

			VkPhysicalDeviceSubgroupProperties subgroupProperties{};
			subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
			subgroupProperties.pNext = &descriptorIndexingProperties;

			VkPhysicalDeviceProperties2 properties{};
			properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			properties.pNext = &subgroupProperties;
			dispatch.vkGetPhysicalDeviceProperties2(physicalDevice.physical_device, &properties);

			VkPhysicalDeviceMemoryProperties memoryProperties{};
			dispatch.vkGetPhysicalDeviceMemoryProperties(physicalDevice.physical_device, &memoryProperties);

			const auto availableExtensions = physicalDevice.get_available_extensions();
			const auto& limits = properties.properties.limits;

			Rhi::AdapterInfo info{};
			info.Name = properties.properties.deviceName;
			info.VendorId = properties.properties.vendorID;
			info.DeviceId = properties.properties.deviceID;

			for (std::uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
			{
				const auto& heap = memoryProperties.memoryHeaps[heapIndex];
				if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
				{
					info.DedicatedVideoMemory += heap.size;
				}
			}

			auto& capabilities = info.Capabilities;
			capabilities.Descriptors.MaxSampledTexturesPerStage = limits.maxPerStageDescriptorSampledImages;
			capabilities.Descriptors.MaxSamplersPerStage = limits.maxPerStageDescriptorSamplers;
			capabilities.Descriptors.MaxStorageTexturesPerStage = limits.maxPerStageDescriptorStorageImages;
			capabilities.Descriptors.MaxUniformBuffersPerStage = limits.maxPerStageDescriptorUniformBuffers;
			capabilities.Descriptors.MaxStorageBuffersPerStage = limits.maxPerStageDescriptorStorageBuffers;
			capabilities.Descriptors.MaxBindlessSampledTextures = descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages;
			capabilities.Descriptors.MaxBindlessSamplers = descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSamplers;

			capabilities.Queues.DedicatedCompute = physicalDevice.has_dedicated_compute_queue();
			capabilities.Queues.DedicatedTransfer = physicalDevice.has_dedicated_transfer_queue();
			capabilities.Queues.AsyncCompute = physicalDevice.has_separate_compute_queue();

			capabilities.MaxPushConstantBytes = limits.maxPushConstantsSize;
			capabilities.MaxColorAttachments = limits.maxColorAttachments;
			capabilities.MaxSamples = GetMaximumSampleCount(
				limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts);
			capabilities.SubgroupSize = subgroupProperties.subgroupSize;
			capabilities.MinUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
			capabilities.MinStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
			if (limits.timestampComputeAndGraphics != VK_FALSE && limits.timestampPeriod > 0.0f)
			{
				capabilities.TimestampFrequency = static_cast<std::uint64_t>(
					std::llround(1'000'000'000.0 / static_cast<double>(limits.timestampPeriod)));
			}

			capabilities.DescriptorIndexing =
				features12.descriptorIndexing != VK_FALSE &&
				features12.runtimeDescriptorArray != VK_FALSE &&
				features12.descriptorBindingPartiallyBound != VK_FALSE;
			capabilities.BufferDeviceAddress = features12.bufferDeviceAddress != VK_FALSE;
			capabilities.IndirectCount = features12.drawIndirectCount != VK_FALSE;
			capabilities.SubgroupOperations = subgroupProperties.supportedOperations != 0;
			capabilities.TimestampQueries = limits.timestampComputeAndGraphics != VK_FALSE;
			capabilities.SparseResidency =
				features.features.sparseBinding != VK_FALSE &&
				(features.features.sparseResidencyBuffer != VK_FALSE ||
				 features.features.sparseResidencyImage2D != VK_FALSE ||
				 features.features.sparseResidencyImage3D != VK_FALSE);
			capabilities.BcTextureCompression = features.features.textureCompressionBC != VK_FALSE;
			capabilities.Etc2TextureCompression = features.features.textureCompressionETC2 != VK_FALSE;
			capabilities.AstcTextureCompression = features.features.textureCompressionASTC_LDR != VK_FALSE;

#ifdef VK_EXT_descriptor_buffer
			capabilities.DescriptorBuffer =
				HasExtension(availableExtensions, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) &&
				descriptorBufferFeatures.descriptorBuffer != VK_FALSE;
#endif
#ifdef VK_EXT_mesh_shader
			capabilities.MeshShaders =
				HasExtension(availableExtensions, VK_EXT_MESH_SHADER_EXTENSION_NAME) &&
				meshShaderFeatures.meshShader != VK_FALSE;
			capabilities.TaskShaders = capabilities.MeshShaders && meshShaderFeatures.taskShader != VK_FALSE;
#endif
#ifdef VK_KHR_ray_query
			capabilities.RayQuery =
				HasExtension(availableExtensions, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
				rayQueryFeatures.rayQuery != VK_FALSE;
#endif
#ifdef VK_KHR_ray_tracing_pipeline
			capabilities.RayTracingPipeline =
				HasExtension(availableExtensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
				rayTracingPipelineFeatures.rayTracingPipeline != VK_FALSE;
#endif
#ifdef VK_EXT_memory_budget
			capabilities.MemoryBudget = HasExtension(availableExtensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif

			return info;
		}

	} // namespace

		class VulkanAdapter final : public Rhi::Adapter
		{
		public:
			VulkanAdapter(
				std::shared_ptr<VulkanInstanceState> instance,
				vkb::PhysicalDevice physicalDevice,
				QueueFamilySelection queueFamilies)
				: instance(std::move(instance)),
				  physicalDevice(std::move(physicalDevice)),
				  queueFamilies(queueFamilies),
				  info(BuildAdapterInfo(this->instance->Dispatch, this->physicalDevice))
			{
			}

			std::uintptr_t GetNativeHandle() const override
			{
				return ToNativeHandle(physicalDevice.physical_device);
			}

			const Rhi::AdapterInfo& GetInfo() const override
			{
				return info;
			}

			std::unique_ptr<Rhi::Device> CreateDevice() override
			{
				if (!queueFamilies.IsValid())
				{
					return nullptr;
				}

				std::vector<vkb::CustomQueueDescription> queueDescriptions;
				std::vector<std::uint32_t> uniqueFamilies;
				for (std::uint32_t family : { queueFamilies.Graphics, queueFamilies.Compute, queueFamilies.Transfer })
				{
					if (std::find(uniqueFamilies.begin(), uniqueFamilies.end(), family) == uniqueFamilies.end())
					{
						uniqueFamilies.push_back(family);
						queueDescriptions.emplace_back(family, std::vector<float>{ 1.0f });
					}
				}

				auto deviceResult = vkb::DeviceBuilder{ physicalDevice }
					.custom_queue_setup(queueDescriptions)
					.build();
				if (!deviceResult)
				{
					return nullptr;
				}

				auto deviceState = std::make_shared<VulkanDeviceState>();
				deviceState->Instance = instance;
				deviceState->Device = std::move(deviceResult).value();
				deviceState->QueueFamilies = queueFamilies;
				volk::volkLoadDeviceTable(&deviceState->Dispatch, deviceState->Device.device);

				deviceState->AllocatorFunctions.vkGetInstanceProcAddr = deviceState->Instance->Instance.fp_vkGetInstanceProcAddr;
				deviceState->AllocatorFunctions.vkGetDeviceProcAddr = deviceState->Device.fp_vkGetDeviceProcAddr;

				VmaAllocatorCreateInfo allocatorInfo{};
				allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
				if (info.Capabilities.MemoryBudget)
				{
					allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
				}
				allocatorInfo.physicalDevice = deviceState->Device.physical_device.physical_device;
				allocatorInfo.device = deviceState->Device.device;
				allocatorInfo.instance = deviceState->Instance->Instance.instance;
				allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
				allocatorInfo.pVulkanFunctions = &deviceState->AllocatorFunctions;
				if (vmaCreateAllocator(&allocatorInfo, &deviceState->Allocator) != VK_SUCCESS)
				{
					return nullptr;
				}

				VkQueue graphicsQueueHandle = VK_NULL_HANDLE;
				VkQueue computeQueueHandle = VK_NULL_HANDLE;
				VkQueue transferQueueHandle = VK_NULL_HANDLE;
				deviceState->Dispatch.vkGetDeviceQueue(deviceState->Device.device, queueFamilies.Graphics, 0, &graphicsQueueHandle);
				deviceState->Dispatch.vkGetDeviceQueue(deviceState->Device.device, queueFamilies.Compute, 0, &computeQueueHandle);
				deviceState->Dispatch.vkGetDeviceQueue(deviceState->Device.device, queueFamilies.Transfer, 0, &transferQueueHandle);
				if (graphicsQueueHandle == VK_NULL_HANDLE || computeQueueHandle == VK_NULL_HANDLE || transferQueueHandle == VK_NULL_HANDLE)
				{
					return nullptr;
				}

				auto graphicsQueueMutex = std::make_shared<std::mutex>();
				deviceState->PresentationQueue = graphicsQueueHandle;
				deviceState->PresentationQueueMutex = graphicsQueueMutex;
				auto computeQueueMutex = computeQueueHandle == graphicsQueueHandle
					? graphicsQueueMutex
					: std::make_shared<std::mutex>();
				std::shared_ptr<std::mutex> transferQueueMutex;
				if (transferQueueHandle == graphicsQueueHandle)
				{
					transferQueueMutex = graphicsQueueMutex;
				}
				else if (transferQueueHandle == computeQueueHandle)
				{
					transferQueueMutex = computeQueueMutex;
				}
				else
				{
					transferQueueMutex = std::make_shared<std::mutex>();
				}

				auto graphicsQueue = std::make_unique<VulkanQueue>(
					deviceState, Rhi::QueueType::Graphics, graphicsQueueHandle, queueFamilies.Graphics, graphicsQueueMutex);
				auto computeQueue = std::make_unique<VulkanQueue>(
					deviceState, Rhi::QueueType::Compute, computeQueueHandle, queueFamilies.Compute, computeQueueMutex);
				auto transferQueue = std::make_unique<VulkanQueue>(
					deviceState, Rhi::QueueType::Transfer, transferQueueHandle, queueFamilies.Transfer, transferQueueMutex);

				return std::make_unique<VulkanDevice>(
					std::move(deviceState),
					info,
					std::move(graphicsQueue),
					std::move(computeQueue),
					std::move(transferQueue));
			}

		private:
			std::shared_ptr<VulkanInstanceState> instance;
			vkb::PhysicalDevice physicalDevice{};
			QueueFamilySelection queueFamilies{};
			Rhi::AdapterInfo info{};
		};

} // namespace Swim::RhiVulkan
