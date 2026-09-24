#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityGraphResources.h"

#include <cstdint>
#include <vector>

namespace Swim::Render
{
	// One ShadowRenderer::Record. Lighting samples Atlas (D32Float, ShaderRead after
	// the depth pass) through Records (one GpuShadowRecord per slot) and Views (one
	// GpuShadowView per tile).
	struct ShadowGraphResources
	{
		GraphTexture Atlas;
		GraphBuffer Records;
		GraphBuffer Views;
		std::vector<VisibilityGraphResources> Visibility; // One caster cull per view.
		GraphPass DepthPass;
		std::uint32_t AtlasSize = 0;
		std::uint32_t ViewCount = 0;
		std::uint32_t RecordCount = 0;
	};
} // namespace Swim::Render
