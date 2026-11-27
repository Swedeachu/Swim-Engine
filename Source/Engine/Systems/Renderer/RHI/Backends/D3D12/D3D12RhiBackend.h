#pragma once

#include "Engine/Systems/Renderer/RHI/RhiFactory.h"

namespace Swim::RhiD3D12
{

	// Null when the D3D12 runtime or DXGI is missing. Adapters without feature level 12.0 are skipped.
	std::unique_ptr<Rhi::GraphicsSystem> CreateGraphicsSystem(const Rhi::GraphicsSystemDesc& desc = {});
	bool RegisterGraphicsBackend(Rhi::GraphicsFactory& factory);

} // namespace Swim::RhiD3D12
