#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render::Internal
{
	struct GraphUse
	{
		std::uint32_t Resource = 0;
		GraphAccess Access = GraphAccess::Read;
		Rhi::ResourceState State = Rhi::ResourceState::Undefined;
		Rhi::TextureSubresourceRange Range{};
	};
} // namespace Swim::Render::Internal
