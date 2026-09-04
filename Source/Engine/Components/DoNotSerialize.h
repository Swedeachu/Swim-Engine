#pragma once

namespace Engine
{

	// Explicit persistence opt-out for runtime/tool-only entities. This marker is
	// scene-authoring policy only; it does not affect simulation or rendering.
	struct DoNotSerialize
	{};

}