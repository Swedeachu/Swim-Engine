#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

namespace Swim::Render
{
	// A readback staging buffer and the transfer pass that initializes it. Read the
	// bytes through RenderGraphExecutor::TryReadback/TryGetReadback after execution.
	struct GraphReadback
	{
		GraphBuffer Buffer;
		GraphPass Pass;
	};
} // namespace Swim::Render
