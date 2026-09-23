#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"

#include <stdexcept>

namespace Swim::Render
{
	void DrawVisibilityBin(Rhi::CommandList& list, Rhi::Buffer& commands, Rhi::Buffer& counts, const VisibilityBinLayout& bins,
		std::uint32_t bin, VisibilityDrawPath path)
	{
		if (bin >= bins.GetBinCount())
		{
			throw std::out_of_range("Visibility bin is out of range");
		}
		const auto& range = bins.GetRange(bin);
		const std::uint64_t offset = std::uint64_t(range.First) * sizeof(Rhi::DrawIndexedIndirectCommand);
		if (path == VisibilityDrawPath::IndirectCount)
		{
			list.DrawIndexedIndirectCount(commands, offset, counts, std::uint64_t(bin) * sizeof(std::uint32_t), range.Capacity);
		}
		else
		{
			list.DrawIndexedIndirect(commands, offset, range.Capacity);
		}
	}
} // namespace Swim::Render
