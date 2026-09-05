#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

namespace Swim::RhiVulkan
{

	std::uint32_t GetColorTexelBytes(Rhi::Format format);
	bool IsIntegerColorFormat(Rhi::Format format);
	VkImageSubresourceRange GetSubresourceRange(const Rhi::TextureDesc& desc, const Rhi::TextureSubresourceRange& range);
	void ValidateCopyExtent(const Rhi::TextureDesc& desc, const Rhi::TextureSubresource& subresource,
		const Rhi::Offset3D& offset, const Rhi::Extent3D& extent);
	VkBufferImageCopy GetBufferImageCopy(const Rhi::BufferDesc& buffer, const Rhi::TextureDesc& texture,
		const Rhi::BufferTextureCopyRegion& region);

} // namespace Swim::RhiVulkan
