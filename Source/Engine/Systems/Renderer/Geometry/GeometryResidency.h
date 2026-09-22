#pragma once
#include "Engine/Systems/Renderer/Resources/GpuUploadState.h"

namespace Swim::Render
{
	// GeometryHeap meshes use the shared GPU upload state machine.
	using GeometryResidency = GpuUploadState;
} // namespace Swim::Render
