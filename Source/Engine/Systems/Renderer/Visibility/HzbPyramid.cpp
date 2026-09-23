#include "Engine/Systems/Renderer/Visibility/HzbPyramid.h"

#include <stdexcept>

namespace Swim::Render
{
	std::vector<HzbExtent> ComputeHzbMips(std::uint32_t width, std::uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			throw std::invalid_argument("HZB source must not be empty");
		}
		std::vector<HzbExtent> mips;
		while (width > 1 || height > 1)
		{
			width = (width + 1) / 2;
			height = (height + 1) / 2;
			mips.push_back({ width, height });
		}
		return mips;
	}

	std::vector<float> HzbReference::Reduce(
		std::span<const float> source, HzbExtent sourceExtent, HzbExtent destination, DepthConvention convention)
	{
		if (source.size() != std::size_t(sourceExtent.Width) * sourceExtent.Height)
		{
			throw std::invalid_argument("HZB reduction source size does not match its extent");
		}
		std::vector<float> result(std::size_t(destination.Width) * destination.Height);
		for (std::uint32_t y = 0; y < destination.Height; ++y)
		{
			for (std::uint32_t x = 0; x < destination.Width; ++x)
			{
				const std::uint32_t sx = x * 2;
				const std::uint32_t sy = y * 2;
				float farthest = source[std::size_t(sy) * sourceExtent.Width + sx];
				for (std::uint32_t dy = 0; dy < 2; ++dy)
				{
					for (std::uint32_t dx = 0; dx < 2; ++dx)
					{
						if (sx + dx < sourceExtent.Width && sy + dy < sourceExtent.Height)
						{
							farthest = FartherDepth(convention, farthest, source[std::size_t(sy + dy) * sourceExtent.Width + sx + dx]);
						}
					}
				}
				result[std::size_t(y) * destination.Width + x] = farthest;
			}
		}
		return result;
	}

	HzbReference HzbReference::Build(std::span<const float> depth, std::uint32_t width, std::uint32_t height, DepthConvention convention)
	{
		HzbReference hzb;
		hzb.width = width;
		hzb.height = height;
		hzb.convention = convention;
		hzb.extents = ComputeHzbMips(width, height);
		hzb.mips.reserve(hzb.extents.size());
		HzbExtent previous{ width, height };
		std::span<const float> source = depth;
		for (const auto& extent : hzb.extents)
		{
			hzb.mips.push_back(Reduce(source, previous, extent, convention));
			source = hzb.mips.back();
			previous = extent;
		}
		return hzb;
	}

	HzbReference HzbReference::FromMips(
		std::uint32_t width, std::uint32_t height, DepthConvention convention, std::vector<std::vector<float>> mips)
	{
		HzbReference hzb;
		hzb.width = width;
		hzb.height = height;
		hzb.convention = convention;
		hzb.extents = ComputeHzbMips(width, height);
		if (mips.size() != hzb.extents.size())
		{
			throw std::invalid_argument("HZB mip count does not match the depth size");
		}
		for (std::size_t mip = 0; mip < mips.size(); ++mip)
		{
			if (mips[mip].size() != std::size_t(hzb.extents[mip].Width) * hzb.extents[mip].Height)
			{
				throw std::invalid_argument("HZB mip size does not match its extent");
			}
		}
		hzb.mips = std::move(mips);
		return hzb;
	}

	float HzbReference::Fetch(std::uint32_t mip, std::uint32_t x, std::uint32_t y) const
	{
		const auto& extent = extents.at(mip);
		if (x >= extent.Width || y >= extent.Height)
		{
			throw std::out_of_range("HZB fetch outside the mip");
		}
		return mips[mip][std::size_t(y) * extent.Width + x];
	}
} // namespace Swim::Render
