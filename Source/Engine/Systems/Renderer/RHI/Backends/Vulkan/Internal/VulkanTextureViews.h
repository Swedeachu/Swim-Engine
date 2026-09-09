#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	// Shared by native view creation and descriptor writes. No mutable formats or 3D slice views.
	bool ValidateVulkanTextureView(const Rhi::TextureDesc& texture, const Rhi::TextureViewDesc& view, bool cubeArrays);

} // namespace Swim::RhiVulkan
