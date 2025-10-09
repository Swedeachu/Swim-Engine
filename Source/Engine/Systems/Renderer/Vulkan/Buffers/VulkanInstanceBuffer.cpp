#include "PCH.h"
#include "VulkanInstanceBuffer.h"

namespace Engine
{

	VulkanInstanceBuffer::VulkanInstanceBuffer
	(
		VkDevice device,
		VkPhysicalDevice physicalDevice,
		size_t instanceSize,
		size_t maxInstances,
		uint32_t framesInFlight
	)
		: device(device),
		physicalDevice(physicalDevice),
		instanceSize(instanceSize),
		maxInstances(maxInstances),
		framesInFlight(framesInFlight)
	{
		alignedInstanceSize = AlignUp(instanceSize, 16); // 16-byte alignment

		VkDeviceSize totalSize = alignedInstanceSize * maxInstances;

		perFrameBuffers.reserve(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			auto buffer = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				totalSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			perFrameBuffers.push_back(std::move(buffer));
		}
	}

	VulkanInstanceBuffer::~VulkanInstanceBuffer()
	{
		Cleanup();
	}

	void* VulkanInstanceBuffer::BeginFrame(uint32_t frameIndex)
	{
		return perFrameBuffers[frameIndex]->GetMappedPointer();
	}

	void VulkanInstanceBuffer::WriteInstance(uint32_t frameIndex, uint32_t instanceIndex, const void* data)
	{
		auto offset = alignedInstanceSize * instanceIndex;

		if (instanceIndex >= maxInstances)
		{
			throw std::runtime_error("WriteInstance overflow (instanceIndex >= maxInstances)");
		}

		perFrameBuffers[frameIndex]->CopyData(data, instanceSize, offset);
	}

	void VulkanInstanceBuffer::Recreate(size_t newMaxInstances)
	{
		maxInstances = newMaxInstances;
		VkDeviceSize totalSize = alignedInstanceSize * maxInstances;

		// Recreate per-frame buffers
		for (auto& buf : perFrameBuffers)
		{
			std::cout << "VulkanInstanceBuffer::Recreate() called" << std::endl;
			// No need to preserve contents: we still have cpuInstanceData on CPU
			auto newBuf = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				totalSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			if (buf)
			{
				buf->Free();
			}

			buf = std::move(newBuf);
		}
	}

	VkBuffer VulkanInstanceBuffer::GetBuffer(uint32_t frameIndex) const
	{
		return perFrameBuffers[frameIndex]->GetBuffer();
	}

	void VulkanInstanceBuffer::Cleanup()
	{
		for (auto& buf : perFrameBuffers)
		{
			if (buf) buf->Free();
		}
		perFrameBuffers.clear();
	}

	size_t VulkanInstanceBuffer::AlignUp(size_t size, size_t alignment) const
	{
		return (size + alignment - 1) & ~(alignment - 1);
	}

}
