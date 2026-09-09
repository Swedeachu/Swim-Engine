#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

namespace Swim::RhiVulkan
{

	VkImageAspectFlags GetVulkanTextureViewAspect(Rhi::Format format, Rhi::TextureAspect aspect);

	// Shared by native view creation and descriptor writes. No mutable formats or 3D slice views.
	bool ValidateVulkanTextureView(const Rhi::TextureDesc& texture, const Rhi::TextureViewDesc& view, bool cubeArrays);

} // namespace Swim::RhiVulkan
