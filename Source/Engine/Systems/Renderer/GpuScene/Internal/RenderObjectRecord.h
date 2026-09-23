#pragma once
#include <cstdint>

namespace Swim::Render::Internal
{
	// Registry-side state of a live render object; the GPU-visible state lives in
	// the instance/transform mirrors at the handle's row.
	struct RenderObjectRecord
	{
		std::uint64_t TransformFrame = 0; // Frame of the last SetTransform (or creation).
	};
} // namespace Swim::Render::Internal
