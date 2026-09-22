#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryGraphResources.h"
#include "Engine/Systems/Renderer/Residency/TextureGraphResources.h"

namespace Swim::Render
{
	// Everything AssetResidencyService::Import added to one graph.
	struct GpuResidencyGraphResources
	{
		GeometryGraphResources Geometry;
		TextureGraphResources Textures;
	};
} // namespace Swim::Render
