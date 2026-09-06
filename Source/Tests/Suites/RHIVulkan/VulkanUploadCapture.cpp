#include "Tests/Fixtures/VulkanUploadCapture.h"
#include "Tests/Framework/Test.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanUploadCapture* active = nullptr;
	}

	VulkanUploadCapture::VulkanUploadCapture(bool coherent)
		: Coherent(coherent)
	{
		active = this;
		State->Instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
		State->Instance->Diagnostics.Echo = false;
		State->Dispatch.vkDeviceWaitIdle = +[](VkDevice) -> VkResult
		{
			++active->IdleCalls;
			return VK_SUCCESS;
		};
		auto& functions = State->AllocatorFunctions;
		functions.vkGetInstanceProcAddr = +[](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; };
		functions.vkGetDeviceProcAddr = +[](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; };
		functions.vkGetPhysicalDeviceProperties = +[](VkPhysicalDevice, VkPhysicalDeviceProperties* properties)
		{
			*properties = {};
			properties->limits.bufferImageGranularity = 1;
			properties->limits.nonCoherentAtomSize = 256;
			properties->limits.maxMemoryAllocationCount = 4096;
		};
		functions.vkGetPhysicalDeviceMemoryProperties = +[](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties)
		{
			*properties = {};
			properties->memoryHeapCount = 1;
			properties->memoryHeaps[0] = { 16ull << 20, 0 };
			properties->memoryTypeCount = 1;
			properties->memoryTypes[0] = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				(active->Coherent ? VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VkMemoryPropertyFlags(0)), 0 };
		};
		functions.vkAllocateMemory = +[](VkDevice, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks*, VkDeviceMemory* memory) -> VkResult
		{
			const auto handle = active->NextMemory++;
			active->Memory[handle].resize(static_cast<std::size_t>(info->allocationSize));
			*memory = RhiVulkan::FromNativeHandle<VkDeviceMemory>(handle);
			return VK_SUCCESS;
		};
		functions.vkFreeMemory = +[](VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*)
		{
			active->Memory.erase(RhiVulkan::ToNativeHandle(memory));
		};
		functions.vkMapMemory = +[](VkDevice, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize, VkMemoryMapFlags, void** data) -> VkResult
		{
			++active->MapCalls;
			if (active->MapResult == VK_SUCCESS)
			{
				*data = active->Memory.at(RhiVulkan::ToNativeHandle(memory)).data() + offset;
			}
			return active->MapResult;
		};
		functions.vkUnmapMemory = +[](VkDevice, VkDeviceMemory) { ++active->UnmapCalls; };
		functions.vkFlushMappedMemoryRanges = +[](VkDevice, std::uint32_t count, const VkMappedMemoryRange* ranges) -> VkResult
		{
			active->Flushes.insert(active->Flushes.end(), ranges, ranges + count);
			return active->FlushResult;
		};
		functions.vkInvalidateMappedMemoryRanges = +[](VkDevice, std::uint32_t, const VkMappedMemoryRange*) -> VkResult { return VK_SUCCESS; };
		functions.vkBindBufferMemory = +[](VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) -> VkResult { return VK_SUCCESS; };
		functions.vkBindImageMemory = +[](VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) -> VkResult { return VK_SUCCESS; };
		functions.vkGetBufferMemoryRequirements = +[](VkDevice, VkBuffer buffer, VkMemoryRequirements* requirements)
		{
			*requirements = { active->Buffers.at(RhiVulkan::ToNativeHandle(buffer)), 16, 1 };
		};
		functions.vkGetImageMemoryRequirements = +[](VkDevice, VkImage, VkMemoryRequirements* requirements) { *requirements = { 16, 16, 1 }; };
		functions.vkCreateBuffer = +[](VkDevice, const VkBufferCreateInfo* info, const VkAllocationCallbacks*, VkBuffer* buffer) -> VkResult
		{
			const auto handle = ++active->CreateCalls;
			active->Buffers[handle] = info->size;
			*buffer = RhiVulkan::FromNativeHandle<VkBuffer>(handle);
			return VK_SUCCESS;
		};
		functions.vkDestroyBuffer = +[](VkDevice, VkBuffer buffer, const VkAllocationCallbacks*)
		{
			++active->DestroyCalls;
			active->Buffers.erase(RhiVulkan::ToNativeHandle(buffer));
		};
		functions.vkCreateImage = +[](VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*) -> VkResult { return VK_ERROR_FEATURE_NOT_PRESENT; };
		functions.vkDestroyImage = +[](VkDevice, VkImage, const VkAllocationCallbacks*) {};
		functions.vkCmdCopyBuffer = +[](VkCommandBuffer, VkBuffer, VkBuffer, std::uint32_t, const VkBufferCopy*) {};
		VmaAllocatorCreateInfo info{};
		info.instance = RhiVulkan::FromNativeHandle<VkInstance>(1);
		info.device = RhiVulkan::FromNativeHandle<VkDevice>(2);
		info.physicalDevice = RhiVulkan::FromNativeHandle<VkPhysicalDevice>(3);
		info.vulkanApiVersion = VK_API_VERSION_1_0;
		info.preferredLargeHeapBlockSize = 4096;
		info.pVulkanFunctions = &functions;
		State->Device.physical_device.physical_device = info.physicalDevice;
		SWIM_REQUIRE_EQUAL(vmaCreateAllocator(&info, &State->Allocator), VK_SUCCESS);
		Device = std::make_unique<RhiVulkan::VulkanDevice>(State, Rhi::AdapterInfo{}, nullptr, nullptr, nullptr);
	}

	VulkanUploadCapture::~VulkanUploadCapture()
	{
		RhiVulkan::RetireLostVulkanDevice(*State);
		Device.reset();
		vmaDestroyAllocator(State->Allocator);
		State->Allocator = nullptr;
		active = nullptr;
	}

} // namespace Swim::Testing
