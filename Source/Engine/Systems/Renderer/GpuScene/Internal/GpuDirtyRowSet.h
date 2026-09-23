#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Swim::Render::Internal
{
	// Deduplicated rows awaiting upload; Take returns them sorted so adjacent rows
	// batch into runs.
	class GpuDirtyRowSet
	{
	  public:
		explicit GpuDirtyRowSet(std::uint32_t capacity = 0) : flags(capacity, false) {}

		void Mark(std::uint32_t row)
		{
			if (!flags[row])
			{
				flags[row] = true;
				rows.push_back(row);
			}
		}

		std::vector<std::uint32_t> Take()
		{
			auto taken = std::move(rows);
			rows.clear();
			std::sort(taken.begin(), taken.end());
			for (auto row : taken)
			{
				flags[row] = false;
			}
			return taken;
		}

		std::uint32_t Size() const { return static_cast<std::uint32_t>(rows.size()); }

	  private:
		std::vector<bool> flags;
		std::vector<std::uint32_t> rows;
	};
} // namespace Swim::Render::Internal
