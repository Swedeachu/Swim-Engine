#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Swim::Render::Internal
{
	// Deduplicated set of CPU-mirror rows awaiting upload.
	struct GeometryDirtyRows
	{
		std::vector<bool> Flags;
		std::vector<std::uint32_t> Rows;

		void Resize(std::uint32_t count) { Flags.assign(count, false); }

		void Mark(std::uint32_t row)
		{
			if (!Flags[row])
			{
				Flags[row] = true;
				Rows.push_back(row);
			}
		}

		void MarkRange(std::uint32_t first, std::uint32_t count)
		{
			for (std::uint32_t row = first; row < first + count; ++row)
			{
				Mark(row);
			}
		}

		// Returns the sorted rows and clears the set.
		std::vector<std::uint32_t> Take()
		{
			auto rows = std::move(Rows);
			Rows.clear();
			std::sort(rows.begin(), rows.end());
			for (auto row : rows)
			{
				Flags[row] = false;
			}
			return rows;
		}
	};
} // namespace Swim::Render::Internal
