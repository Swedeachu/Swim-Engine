#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanPipelineCacheData.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	namespace
	{
		constexpr std::array<std::byte, 8> magic{ std::byte{'S'}, std::byte{'W'}, std::byte{'I'}, std::byte{'M'},
			std::byte{'P'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'} };

		std::uint64_t Read(std::span<const std::byte> bytes, std::size_t offset, std::size_t size)
		{
			std::uint64_t value = 0;
			for (std::size_t index = 0; index < size; ++index)
			{
				value |= std::uint64_t(std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8);
			}
			return value;
		}

		void Write(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value, std::size_t size)
		{
			for (std::size_t index = 0; index < size; ++index)
			{
				bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 255);
			}
		}

		std::uint64_t Checksum(std::span<const std::byte> data)
		{
			std::uint64_t hash = 14695981039346656037ull;
			for (std::size_t index = 0; index < data.size(); ++index)
			{
				// Checksum covers metadata and opaque payload, excluding its own field.
				if (index < 48 || index >= VulkanPipelineCacheEnvelopeBytes)
				{
					hash = (hash ^ std::to_integer<std::uint8_t>(data[index])) * 1099511628211ull;
				}
			}
			return hash;
		}

		bool MatchesUuid(std::span<const std::byte> bytes, std::size_t offset, const VkPhysicalDeviceProperties& properties)
		{
			for (std::size_t index = 0; index < VK_UUID_SIZE; ++index)
			{
				if (std::to_integer<std::uint8_t>(bytes[offset + index]) != properties.pipelineCacheUUID[index])
				{
					return false;
				}
			}
			return true;
		}
	}

	bool IsCompatibleVulkanPipelineCacheHeader(std::span<const std::byte> data, const VkPhysicalDeviceProperties& properties)
	{
		// Vulkan's version-one header is a 32-byte little-endian byte layout,
		// independent of C++ structure alignment and native pointer size.
		return data.size() >= 32 && Read(data, 0, 4) == 32 && Read(data, 4, 4) == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
			Read(data, 8, 4) == properties.vendorID && Read(data, 12, 4) == properties.deviceID && MatchesUuid(data, 16, properties);
	}

	Rhi::PipelineCacheLoadStatus DecodeVulkanPipelineCacheData(std::span<const std::byte> data,
		const VkPhysicalDeviceProperties& properties, std::span<const std::byte>& nativeData)
	{
		nativeData = {};
		if (data.empty())
		{
			return Rhi::PipelineCacheLoadStatus::Empty;
		}
		if (data.size() < VulkanPipelineCacheEnvelopeBytes + 32 || data.size() > Rhi::MaxPipelineCacheDataBytes ||
			!std::equal(magic.begin(), magic.end(), data.begin()) || Read(data, 8, 4) != 1 ||
			Read(data, 40, 8) != data.size() - VulkanPipelineCacheEnvelopeBytes || Read(data, 48, 8) != Checksum(data))
		{
			return Rhi::PipelineCacheLoadStatus::InvalidData;
		}
		const auto payload = data.subspan(VulkanPipelineCacheEnvelopeBytes);
		if (Read(data, 12, 4) != properties.vendorID || Read(data, 16, 4) != properties.deviceID ||
			Read(data, 20, 4) != properties.driverVersion || !MatchesUuid(data, 24, properties) ||
			!IsCompatibleVulkanPipelineCacheHeader(payload, properties))
		{
			return Rhi::PipelineCacheLoadStatus::Incompatible;
		}
		nativeData = payload;
		return Rhi::PipelineCacheLoadStatus::Loaded;
	}

	std::vector<std::byte> EncodeVulkanPipelineCacheData(std::span<const std::byte> nativeData,
		const VkPhysicalDeviceProperties& properties)
	{
		if (nativeData.size() > Rhi::MaxPipelineCacheDataBytes - VulkanPipelineCacheEnvelopeBytes ||
			!IsCompatibleVulkanPipelineCacheHeader(nativeData, properties))
		{
			throw std::invalid_argument("Invalid Vulkan pipeline cache payload");
		}
		std::vector<std::byte> data(VulkanPipelineCacheEnvelopeBytes + nativeData.size());
		std::copy(magic.begin(), magic.end(), data.begin());
		Write(data, 8, 1, 4);
		Write(data, 12, properties.vendorID, 4);
		Write(data, 16, properties.deviceID, 4);
		Write(data, 20, properties.driverVersion, 4);
		for (std::size_t index = 0; index < VK_UUID_SIZE; ++index)
		{
			data[24 + index] = static_cast<std::byte>(properties.pipelineCacheUUID[index]);
		}
		Write(data, 40, nativeData.size(), 8);
		std::copy(nativeData.begin(), nativeData.end(), data.begin() + VulkanPipelineCacheEnvelopeBytes);
		Write(data, 48, Checksum(data), 8);
		return data;
	}

} // namespace Swim::RhiVulkan
