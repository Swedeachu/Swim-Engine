#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Platform/Internal/VulkanWsi.h"
#include "Engine/Platform/Window.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanQueueFamilies.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanAdapter.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanGraphicsSystem.h"

#include <volk.h>
#include <VkBootstrap.h>

#include <memory>
#include <vector>

namespace Swim::RhiVulkan
{

	namespace
	{

		VkPhysicalDeviceVulkan12Features GetRequiredVulkan12Features()
		{
			VkPhysicalDeviceVulkan12Features features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			features.drawIndirectCount = VK_TRUE;
			features.descriptorIndexing = VK_TRUE;
			features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
			features.descriptorBindingPartiallyBound = VK_TRUE;
			features.descriptorBindingVariableDescriptorCount = VK_TRUE;
			features.runtimeDescriptorArray = VK_TRUE;
			features.bufferDeviceAddress = VK_TRUE;
			features.timelineSemaphore = VK_TRUE;
			return features;
		}

		VkPhysicalDeviceVulkan13Features GetRequiredVulkan13Features()
		{
			VkPhysicalDeviceVulkan13Features features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			features.synchronization2 = VK_TRUE;
			features.dynamicRendering = VK_TRUE;
			return features;
		}

	} // namespace

	std::unique_ptr<Rhi::GraphicsSystem> CreateGraphicsSystem(const Rhi::GraphicsSystemDesc& desc)
	{
		auto log = desc.Diagnostics ? desc.Diagnostics : std::make_shared<Rhi::DiagnosticLog>();
		if (!Platform::Internal::AcquireVulkanLoader())
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "VulkanLoader", "SDL could not load the Vulkan loader");
			return nullptr;
		}

		auto instance = std::make_shared<VulkanInstanceState>();
		instance->LoaderAcquired = true;
		instance->Diagnostics.Log = log;
		instance->Diagnostics.Echo = desc.EchoDiagnostics;

		auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			Platform::Internal::GetVulkanInstanceProcAddress());
		if (!getInstanceProcAddr)
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "VulkanLoader", "Vulkan instance procedure address is unavailable");
			return nullptr;
		}

		const auto requiredExtensions = Platform::Internal::GetVulkanInstanceExtensions();
		if (requiredExtensions.empty())
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "VulkanWSI", "SDL did not report Vulkan instance extensions");
			return nullptr;
		}

		volk::volkInitializeCustom(getInstanceProcAddr);

		vkb::InstanceBuilder instanceBuilder{ getInstanceProcAddr };
		instanceBuilder
			.set_app_name("Swim Engine")
			.set_engine_name("Swim Engine")
			.require_api_version(1, 3, 0)
			.set_headless(true);

		for (const char* extension : requiredExtensions)
		{
			instanceBuilder.enable_extension(extension);
		}

		if (!ConfigureInstanceDiagnostics(instanceBuilder, instance->Diagnostics, desc.Validation, getInstanceProcAddr))
		{
			return nullptr;
		}

		auto instanceResult = instanceBuilder.build();
		if (!instanceResult)
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "VulkanInstance", instanceResult.error().message());
			return nullptr;
		}

		instance->Instance = std::move(instanceResult).value();
		if (instance->Diagnostics.DebugUtilsEnabled && instance->Instance.debug_messenger == VK_NULL_HANDLE)
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "DebugMessenger", "Vulkan debug messenger was not created");
			return nullptr;
		}
		volk::volkLoadInstanceTable(&instance->Dispatch, instance->Instance.instance);

		auto selector = vkb::PhysicalDeviceSelector{ instance->Instance };
		selector
			.require_present(false)
			.set_minimum_version(1, 3)
			.add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
			.set_required_features_12(GetRequiredVulkan12Features())
			.set_required_features_13(GetRequiredVulkan13Features())
			.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
			.allow_any_gpu_device_type(true);

		auto physicalDevicesResult = selector.select_devices();
		if (!physicalDevicesResult)
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "VulkanAdapter", physicalDevicesResult.error().message());
			return nullptr;
		}

		auto physicalDevices = std::move(physicalDevicesResult).value();
		if (physicalDevices.empty())
		{
			return nullptr;
		}

		std::vector<std::unique_ptr<VulkanAdapter>> adapters;
		adapters.reserve(physicalDevices.size());
		for (auto& physicalDevice : physicalDevices)
		{
			const QueueFamilySelection queueFamilies = SelectQueueFamilies(
				instance->Instance.instance, physicalDevice);
			if (!queueFamilies.IsValid())
			{
				continue;
			}

#ifdef VK_EXT_memory_budget
			physicalDevice.enable_extension_if_present(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif
			adapters.push_back(std::make_unique<VulkanAdapter>(
				instance, std::move(physicalDevice), queueFamilies));
		}

		if (adapters.empty())
		{
			log->Record(Rhi::DiagnosticSeverity::Error, "VulkanAdapter", "No adapter satisfies the required graphics/presentation queue selection");
			return nullptr;
		}

		return std::make_unique<VulkanGraphicsSystem>(std::move(instance), std::move(adapters));
	}

	bool RegisterGraphicsBackend(Rhi::GraphicsFactory& factory)
	{
		return factory.Register(Rhi::GraphicsApi::Vulkan, &CreateGraphicsSystem);
	}

} // namespace Swim::RhiVulkan
