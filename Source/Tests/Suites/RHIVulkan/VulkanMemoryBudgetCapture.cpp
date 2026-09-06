#include "Tests/Fixtures/VulkanMemoryBudgetCapture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>

namespace Swim::Testing
{

	namespace
	{
		VulkanMemoryBudgetCapture* active = nullptr;
	}

	VulkanMemoryBudgetCapture::VulkanMemoryBudgetCapture(bool driverBudget)
	{
		active = this;
		Properties.memoryHeapCount = 2;
		Properties.memoryHeaps[0] = { 1ull << 30, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
		Properties.memoryHeaps[1] = { 2ull << 30, 0 };
		Properties.memoryTypeCount = 3;
		Properties.memoryTypes[0] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 };
		Properties.memoryTypes[1] = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 };
		Properties.memoryTypes[2] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0 };
		Driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
		Driver.heapBudget[0] = 512ull << 20;
		Driver.heapBudget[1] = 1ull << 30;
		Driver.heapUsage[0] = 16ull << 20;
		Driver.heapUsage[1] = 32ull << 20;
		State->Instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
		State->Instance->Diagnostics.Echo = false;
		State->MemoryBudgetEnabled = driverBudget;
		State->Dispatch.vkDeviceWaitIdle = +[](VkDevice) -> VkResult
		{
			++active->IdleCalls;
			return VK_SUCCESS;
		};
		State->Instance->Dispatch.vkGetPhysicalDeviceMemoryProperties2 = +[](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2* properties)
		{
			++active->DriverCalls;
			properties->memoryProperties = active->Properties;
			auto* budget = static_cast<VkPhysicalDeviceMemoryBudgetPropertiesEXT*>(properties->pNext);
			if (budget != nullptr)
			{
				SWIM_CHECK_EQUAL(budget->sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
				*budget = active->Driver;
			}
			if (active->LoseDuringQuery)
			{
				RhiVulkan::ObserveVulkanResult(*active->State, VK_ERROR_DEVICE_LOST, "concurrent loss");
			}
		};
		auto& functions = State->AllocatorFunctions;
		functions.vkGetInstanceProcAddr = +[](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; };
		functions.vkGetDeviceProcAddr = +[](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; };
		functions.vkGetPhysicalDeviceProperties = +[](VkPhysicalDevice, VkPhysicalDeviceProperties* properties)
		{
			*properties = {};
			properties->limits.bufferImageGranularity = 1;
			properties->limits.nonCoherentAtomSize = 1;
			properties->limits.maxMemoryAllocationCount = 4096;
		};
		functions.vkGetPhysicalDeviceMemoryProperties = +[](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties)
		{
			*properties = active->Properties;
		};
		functions.vkGetPhysicalDeviceMemoryProperties2KHR = State->Instance->Dispatch.vkGetPhysicalDeviceMemoryProperties2;
		functions.vkGetPhysicalDeviceProperties2KHR = +[](VkPhysicalDevice device, VkPhysicalDeviceProperties2* properties)
		{
			active->State->AllocatorFunctions.vkGetPhysicalDeviceProperties(device, &properties->properties);
		};
		functions.vkAllocateMemory = +[](VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* memory) -> VkResult
		{
			*memory = RhiVulkan::FromNativeHandle<VkDeviceMemory>(++active->NativeAllocations);
			return VK_SUCCESS;
		};
		functions.vkFreeMemory = +[](VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) { ++active->NativeFrees; };
		functions.vkMapMemory = +[](VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**) -> VkResult { return VK_ERROR_MEMORY_MAP_FAILED; };
		functions.vkUnmapMemory = +[](VkDevice, VkDeviceMemory) {};
		functions.vkFlushMappedMemoryRanges = +[](VkDevice, std::uint32_t, const VkMappedMemoryRange*) -> VkResult { return VK_SUCCESS; };
		functions.vkInvalidateMappedMemoryRanges = functions.vkFlushMappedMemoryRanges;
		functions.vkBindBufferMemory = +[](VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) -> VkResult { return VK_SUCCESS; };
		functions.vkBindImageMemory = +[](VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) -> VkResult { return VK_SUCCESS; };
		functions.vkGetBufferMemoryRequirements = +[](VkDevice, VkBuffer, VkMemoryRequirements* requirements) { *requirements = { 16, 16, 1 }; };
		functions.vkGetImageMemoryRequirements = +[](VkDevice, VkImage, VkMemoryRequirements* requirements) { *requirements = { 16, 16, 1 }; };
		functions.vkCreateBuffer = +[](VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*) -> VkResult { return VK_ERROR_FEATURE_NOT_PRESENT; };
		functions.vkCreateImage = +[](VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*) -> VkResult { return VK_ERROR_FEATURE_NOT_PRESENT; };
		functions.vkDestroyBuffer = +[](VkDevice, VkBuffer, const VkAllocationCallbacks*) {};
		functions.vkDestroyImage = +[](VkDevice, VkImage, const VkAllocationCallbacks*) {};
		functions.vkCmdCopyBuffer = +[](VkCommandBuffer, VkBuffer, VkBuffer, std::uint32_t, const VkBufferCopy*) {};
		VmaAllocatorCreateInfo info{};
		info.instance = RhiVulkan::FromNativeHandle<VkInstance>(1);
		info.device = RhiVulkan::FromNativeHandle<VkDevice>(2);
		info.physicalDevice = RhiVulkan::FromNativeHandle<VkPhysicalDevice>(3);
		info.vulkanApiVersion = VK_API_VERSION_1_0;
		info.flags = driverBudget ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0;
		info.pVulkanFunctions = &functions;
		State->Device.physical_device.physical_device = info.physicalDevice;
		SWIM_REQUIRE_EQUAL(vmaCreateAllocator(&info, &State->Allocator), VK_SUCCESS);
		Device = std::make_unique<RhiVulkan::VulkanDevice>(State, Rhi::AdapterInfo{}, nullptr, nullptr, nullptr);
		DriverCalls = 0;
	}

	VulkanMemoryBudgetCapture::~VulkanMemoryBudgetCapture()
	{
		RhiVulkan::RetireLostVulkanDevice(*State);
		for (auto allocation : allocations)
		{
			vmaFreeMemory(State->Allocator, allocation);
		}
		vmaDestroyAllocator(State->Allocator);
		State->Allocator = nullptr;
	}

	VmaAllocation VulkanMemoryBudgetCapture::Allocate(std::uint64_t bytes, std::uint32_t memoryType, bool dedicated)
	{
		VkMemoryRequirements requirements{ bytes, 16, 1u << memoryType };
		VmaAllocationCreateInfo info{};
		info.flags = dedicated ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0;
		VmaAllocation allocation = nullptr;
		SWIM_REQUIRE_EQUAL(vmaAllocateMemory(State->Allocator, &requirements, &info, &allocation, nullptr), VK_SUCCESS);
		allocations.push_back(allocation);
		return allocation;
	}

	void VulkanMemoryBudgetCapture::Free(VmaAllocation allocation)
	{
		vmaFreeMemory(State->Allocator, allocation);
		std::erase(allocations, allocation);
	}

} // namespace Swim::Testing
