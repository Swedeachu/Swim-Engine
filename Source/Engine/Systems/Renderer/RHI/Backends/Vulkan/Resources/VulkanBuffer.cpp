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

	std::span<std::byte> VulkanBuffer::GetMappedWriteSpan()
	{
		RequireVulkanDevice(*state);
		if (mappedData == nullptr)
		{
			return {};
		}
		return { mappedData, static_cast<std::size_t>(desc.Size) };
	}

	void VulkanBuffer::FlushMappedWrites(std::uint64_t offset, std::uint64_t size)
	{
		RequireVulkanDevice(*state);
		if (mappedData == nullptr || desc.Memory != Rhi::MemoryPreference::CpuToGpu ||
			offset > desc.Size || size > desc.Size - offset)
		{
			throw std::invalid_argument("Mapped flush requires an in-bounds persistent CpuToGpu range");
		}
		// VMA aligns the allocation-relative range to nonCoherentAtomSize and
		// skips the native flush on coherent memory. Mapping alone does not flush.
		if (size != 0 && CheckVulkanResult(*state,
			vmaFlushAllocation(state->Allocator, allocation, offset, size), "vmaFlushAllocation") != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to flush Vulkan buffer allocation");
		}
	}

} // namespace Swim::RhiVulkan
