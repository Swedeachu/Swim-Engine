#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <cstdint>

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

		// Writes data to the mapped memory (must be within range)
		void CopyData(const void* data, size_t size, size_t offset = 0);

		VkBuffer GetBuffer() const { return buffer; }
		VkDeviceMemory GetMemory() const { return memory; }

		// Optionally expose mapped pointer for manual writes
		void* GetMappedPointer() const { return mappedPtr; }

		bool IsValid() const { return buffer != VK_NULL_HANDLE && memory != VK_NULL_HANDLE; }

		template<typename T>
		void ReadData(T* dst, size_t count = 1, size_t offsetBytes = 0) const
		{
			if (!mappedPtr)
			{
				throw std::runtime_error("Buffer is not mapped!");
			}
			memcpy(dst, static_cast<const char*>(mappedPtr) + offsetBytes, count * sizeof(T));
		}

	private:

		VkDevice device;
		VkPhysicalDevice physicalDevice;
		VkBuffer buffer;
		VkDeviceMemory memory;

		// Persistent pointer to mapped memory
		void* mappedPtr = nullptr;  

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

	};

}
