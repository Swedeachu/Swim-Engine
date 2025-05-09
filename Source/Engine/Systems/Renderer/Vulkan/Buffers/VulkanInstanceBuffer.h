#pragma once

#include "VulkanBuffer.h"
#include <vector>
#include <memory>

namespace Engine
{

	class VulkanInstanceBuffer
	{

	public:

		VulkanInstanceBuffer(
			VkDevice device,
			VkPhysicalDevice physicalDevice,
			size_t instanceSize,
			size_t maxInstances,
			uint32_t framesInFlight
		);

		~VulkanInstanceBuffer();

		// Begins the frame, returns a writable pointer
		void* BeginFrame(uint32_t frameIndex);

		// Optional single-instance write (if not writing manually)
		void WriteInstance(uint32_t frameIndex, uint32_t instanceIndex, const void* data);

		VkBuffer GetBuffer(uint32_t frameIndex) const;

		const std::vector<std::unique_ptr<VulkanBuffer>>& GetPerFrameBuffers() const
		{
			return perFrameBuffers;
		}

		VulkanBuffer* GetBufferRaw(uint32_t frameIndex) const
		{
			return perFrameBuffers[frameIndex].get();
		}

		void Cleanup();

	private:

		VkDevice device;
		VkPhysicalDevice physicalDevice;

		size_t instanceSize;
		size_t alignedInstanceSize;
		size_t maxInstances;
		uint32_t framesInFlight;

		std::vector<std::unique_ptr<VulkanBuffer>> perFrameBuffers;

		size_t AlignUp(size_t size, size_t alignment) const;

	};

}
