#pragma once

#include <stdexcept>

namespace Engine
{

	class VulkanBuffer
	{

	public:

		VulkanBuffer
		(
			VkDevice device, 
			VkPhysicalDevice physicalDevice,
			VkDeviceSize size,
			VkBufferUsageFlags usage, 
			VkMemoryPropertyFlags properties
		);

		~VulkanBuffer();

		void Free();

		void CopyData(const void* data, size_t size);

		VkBuffer GetBuffer() const { return buffer; }
		VkDeviceMemory GetMemory() const { return memory; }

	private:

		VkDevice device;
		VkPhysicalDevice physicalDevice;
		VkBuffer buffer;
		VkDeviceMemory memory;

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

	};

}