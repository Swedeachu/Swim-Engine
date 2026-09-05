#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanAdapterInfo.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace Swim::RhiVulkan
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

		VkPhysicalDeviceDriverProperties driver{};
		driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		descriptorIndexingProperties.pNext = &driver;

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
		info.DriverName = driver.driverName;
		info.DriverInfo = driver.driverInfo;
		info.DriverVersion = properties.properties.driverVersion;
		info.ApiVersion = std::to_string(VK_API_VERSION_MAJOR(properties.properties.apiVersion)) + "." +
			std::to_string(VK_API_VERSION_MINOR(properties.properties.apiVersion)) + "." +
			std::to_string(VK_API_VERSION_PATCH(properties.properties.apiVersion));
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

	void ReportAdapterInfo(VulkanDiagnosticsState& diagnostics, const Rhi::AdapterInfo& info)
	{
		std::ostringstream text;
		text << info.Name << "; Vulkan " << info.ApiVersion << "; driver " << info.DriverName
			<< " (" << info.DriverInfo << "), raw version " << info.DriverVersion
			<< "; vendor/device " << std::hex << info.VendorId << "/" << info.DeviceId;
		if (diagnostics.Log)
		{
			diagnostics.Log->Record(Rhi::DiagnosticSeverity::Info, "Adapter", text.str());
		}
		if (diagnostics.Echo)
		{
			std::fprintf(stderr, "[Swim Vulkan] %s\n", text.str().c_str());
		}
	}

} // namespace Swim::RhiVulkan
