#pragma once
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphResource.h"

namespace Swim::Render::Internal
{
	struct GraphPooledResource
	{
		GraphResource Description;
		std::unique_ptr<Rhi::RhiObject> Object;
	};
} // namespace Swim::Render::Internal
