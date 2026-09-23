#pragma once
#include <cstdint>

namespace Swim::Render
{
	struct GpuSceneStats
	{
		std::uint32_t LiveObjects = 0;
		std::uint32_t RetiringObjects = 0;
		std::uint32_t RowCount = 0; // Rows ever used; bounds GPU iteration.
		std::uint32_t Capacity = 0;
		std::uint32_t DirtyInstanceRows = 0;
		std::uint32_t DirtyTransformRows = 0;
		std::uint32_t SettlingTransforms = 0; // Moved last frame; Previous catches up next import.
		std::uint32_t LastInstanceRows = 0;	  // Rows uploaded by the last import.
		std::uint32_t LastTransformRows = 0;
		std::uint32_t LastUploadRuns = 0; // Contiguous row runs (one copy each).
		std::uint64_t LastUploadBytes = 0;
		std::uint64_t TotalUploadBytes = 0;
		std::uint64_t Frame = 0; // Imports so far.
	};
} // namespace Swim::Render
