#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"

#include <cstdint>
#include <vector>

namespace Swim::Render
{
	// Output of HzbBuilder::Record: a transient R32Float pyramid whose mip i is depth
	// level i + 1 (see HzbPyramid.h). Later passes read every mip as ShaderRead.
	struct HzbGraphResources
	{
		GraphTexture Pyramid;
		std::uint32_t Width = 0; // Depth (level 0) size.
		std::uint32_t Height = 0;
		std::uint32_t MipCount = 0; // Pyramid mips.
		DepthConvention Convention = CanonicalDepthConvention;
		std::vector<GraphPass> Passes; // One reduction pass per mip.
	};
} // namespace Swim::Render
