#pragma once

#include "Engine/Systems/Scene/RenderExtraction/RenderExtractorDesc.h"

namespace Engine
{

	// World transform source for RenderExtractor backed by Engine::Transform:
	// the cached world matrix (parent chain included) as a render affine. Entities
	// without a Transform are placed at the origin.
	RenderWorldTransformSource MakeTransformWorldSource();

} // namespace Engine
