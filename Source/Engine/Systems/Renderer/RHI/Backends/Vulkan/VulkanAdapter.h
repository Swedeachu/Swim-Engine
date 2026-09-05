#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanAdapterInfo.h"
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
				ReportAdapterInfo(this->instance->Diagnostics, info);
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
				deviceState->QueueProperties = deviceState->Device.physical_device.get_queue_families();
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

				SetVulkanObjectName(*deviceState, VK_OBJECT_TYPE_DEVICE, ToNativeHandle(deviceState->Device.device), "Swim device");
				SetVulkanObjectName(*deviceState, VK_OBJECT_TYPE_QUEUE, ToNativeHandle(graphicsQueueHandle), "Swim graphics/present queue");
				if (computeQueueHandle != graphicsQueueHandle)
				{
					SetVulkanObjectName(*deviceState, VK_OBJECT_TYPE_QUEUE, ToNativeHandle(computeQueueHandle), "Swim compute queue");
				}
				if (transferQueueHandle != graphicsQueueHandle && transferQueueHandle != computeQueueHandle)
				{
					SetVulkanObjectName(*deviceState, VK_OBJECT_TYPE_QUEUE, ToNativeHandle(transferQueueHandle), "Swim transfer queue");
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
