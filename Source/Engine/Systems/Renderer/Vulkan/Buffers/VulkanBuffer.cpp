#include "PCH.h"
#include "VulkanBuffer.h"

namespace Engine
{

	VulkanBuffer::VulkanBuffer
	(
		VkDevice device,
		VkPhysicalDevice physicalDevice,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties
	)
		: device(device),
		physicalDevice(physicalDevice),
		buffer(VK_NULL_HANDLE),
		memory(VK_NULL_HANDLE),
		mappedPtr(nullptr)
	{
		// 1. Create buffer
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create buffer!");
		}

		sizeBytes = size;

		// 2. Get memory requirements
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

		// 3. Allocate memory
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate buffer memory!");
		}

		// 4. Bind memory to buffer
		if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to bind buffer memory!");
		}

		// 5. If host visible, map it persistently
		if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			if (vkMapMemory(device, memory, 0, size, 0, &mappedPtr) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to map buffer memory!");
			}
		}
	}

	VulkanBuffer::~VulkanBuffer()
	{
		Free();
	}

	void VulkanBuffer::Free()
	{
		if (mappedPtr)
		{
			vkUnmapMemory(device, memory);
			mappedPtr = nullptr;
		}

		if (memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, memory, nullptr);
			memory = VK_NULL_HANDLE;
		}

		if (buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
		}

		sizeBytes = 0;
	}

	void VulkanBuffer::CopyData(const void* data, size_t size, size_t offset)
	{
		if (!mappedPtr)
		{
			throw std::runtime_error("Buffer memory is not mapped!");
		}
		if (offset + size > sizeBytes)
		{
			throw std::runtime_error("CopyData overflow (offset + size exceeds buffer)");
		}

		memcpy(static_cast<char*>(mappedPtr) + offset, data, size);

		// Optional: flush if not HOST_COHERENT (not required with HOST_COHERENT_BIT set)
		// Use vkFlushMappedMemoryRanges() if needed in future
	}

	uint32_t VulkanBuffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		{
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type!");
	}

}
