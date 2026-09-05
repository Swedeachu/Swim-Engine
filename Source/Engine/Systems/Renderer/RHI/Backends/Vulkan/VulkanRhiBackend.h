#pragma once

#include "Engine/Systems/Renderer/RHI/RhiFactory.h"

namespace Swim::RhiVulkan
{

	std::unique_ptr<Rhi::GraphicsSystem> CreateGraphicsSystem();
	bool RegisterGraphicsBackend(Rhi::GraphicsFactory& factory);

} // namespace Swim::RhiVulkan
