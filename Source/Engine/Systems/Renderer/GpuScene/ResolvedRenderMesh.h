#pragma once
#include "Engine/Systems/Renderer/GpuScene/RenderBounds.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

namespace Swim::Render
{
	// A GPU-resident mesh ready to be referenced by render objects, with the
	// local bounds its instances cull against.
	struct ResolvedRenderMesh
	{
		GpuMeshHandle Mesh{};
		RenderBounds LocalBounds = RenderBounds::Infinite();
	};
} // namespace Swim::Render
