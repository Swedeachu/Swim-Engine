#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render
{
	struct GraphBarrier
	{
		std::uint32_t Resource = 0;
		Rhi::ResourceState Before = Rhi::ResourceState::Undefined;
		Rhi::ResourceState After = Rhi::ResourceState::Undefined;
		Rhi::TextureSubresourceRange Range{};
	};
} // namespace Swim::Render
