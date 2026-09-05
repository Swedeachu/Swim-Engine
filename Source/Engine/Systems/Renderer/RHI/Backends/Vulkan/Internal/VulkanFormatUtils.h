#pragma once

// Rhi <-> Vulkan format, sample-count and usage-flag conversions, plus the
// texture-desc validation shared by the swapchain and device resource
// factories. Pure, stateless helpers with no Vulkan device dependency.

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::RhiVulkan
{

	std::uint32_t GetMaximumSampleCount(VkSampleCountFlags counts);
	bool HasExtension(const std::vector<std::string>& extensions, std::string_view name);

	VkFormat ToVkFormat(Rhi::Format format);
	Rhi::Format FromVkFormat(VkFormat format);
	VkSampleCountFlagBits ToVkSampleCount(Rhi::SampleCount samples);

	bool HasBufferUsage(Rhi::BufferUsage value, Rhi::BufferUsage flag);
	bool HasTextureUsage(Rhi::TextureUsage value, Rhi::TextureUsage flag);
	VkBufferUsageFlags ToVkBufferUsage(Rhi::BufferUsage usage);
	VkImageUsageFlags ToVkImageUsage(Rhi::TextureUsage usage);
	VkImageAspectFlags GetImageAspectMask(Rhi::Format format);
	VkImageType ToVkImageType(Rhi::TextureDimension dimension);
	VkImageViewType ToVkImageViewType(Rhi::TextureViewDimension dimension);

	bool ValidateTextureDesc(const Rhi::TextureDesc& desc);

} // namespace Swim::RhiVulkan
