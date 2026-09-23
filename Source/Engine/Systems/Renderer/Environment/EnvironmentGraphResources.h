#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <cstdint>
#include <vector>

namespace Swim::Render
{
	// Output of EnvironmentBuilder::RecordFromSource. Shaders read the prefiltered
	// cube (TextureCube, all mips) and the irradiance buffer (9 float4) as ShaderRead;
	// the BRDF LUT is recorded separately (EnvironmentBuilder::RecordBrdfLut).
	struct EnvironmentGraphResources
	{
		GraphTexture Source;	  // RGBA16Float cube, EnvironmentSourceMipCount mips.
		GraphTexture Prefiltered; // RGBA16Float cube; mip m holds roughness m / (mips - 1).
		GraphBuffer Irradiance;	  // Irradiance / pi, order-2 SH (EnvironmentIrradianceBindings::OutputBytes).
		std::uint32_t SourceSize = 0;
		std::uint32_t SourceMipCount = 0;
		std::uint32_t PrefilteredSize = 0;
		std::uint32_t PrefilteredMipCount = 0;
		std::vector<GraphPass> Passes; // Sky and mip passes (when recorded), prefilter mips, irradiance.
	};
} // namespace Swim::Render
