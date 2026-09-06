#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"

#include <stdexcept>

namespace Swim::RhiVulkan
{

	VulkanBuffer::~VulkanBuffer()
	{
		RetireLostVulkanDevice(*state);
		if (buffer != VK_NULL_HANDLE && allocation != nullptr)
		{
			vmaDestroyBuffer(state->Allocator, buffer, allocation);
		}
	}

	void VulkanBuffer::Write(std::uint64_t offset, std::span<const std::byte> data)
	{
		RequireVulkanDevice(*state);
		if (desc.Memory != Rhi::MemoryPreference::CpuToGpu || offset > desc.Size || data.size() > desc.Size - offset)
		{
			throw std::invalid_argument("Vulkan buffer Write requires an in-bounds CpuToGpu range");
		}
		if (!data.empty() && CheckVulkanResult(*state, vmaCopyMemoryToAllocation(state->Allocator, data.data(), allocation, offset, data.size()), "vmaCopyMemoryToAllocation") != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to write Vulkan buffer allocation");
		}
	}

	void VulkanBuffer::Read(std::uint64_t offset, std::span<std::byte> data)
	{
		RequireVulkanDevice(*state);
		if (desc.Memory != Rhi::MemoryPreference::GpuToCpu || offset > desc.Size || data.size() > desc.Size - offset)
		{
			throw std::invalid_argument("Vulkan buffer Read requires an in-bounds GpuToCpu range");
		}
		if (!data.empty() && CheckVulkanResult(*state, vmaCopyAllocationToMemory(state->Allocator, allocation, offset, data.data(), data.size()), "vmaCopyAllocationToMemory") != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to read Vulkan buffer allocation");
		}
	}

} // namespace Swim::RhiVulkan
