#pragma once
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Swim::Render
{
	struct HzbExtent
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	// Mip i of an HZB pyramid is depth level i + 1: each level halves (rounding up)
	// until 1x1, and texel (x, y) of level L covers depth texels [x * 2^L, (x + 1) * 2^L)
	// in each axis (clipped to the depth size). Empty for a 1x1 depth.
	std::vector<HzbExtent> ComputeHzbMips(std::uint32_t width, std::uint32_t height);

	// CPU definition of the HZB (HzbReduce.slang): every texel keeps the farthest
	// depth of the 2x2 (or clipped) footprint below it.
	class HzbReference
	{
	  public:
		static HzbReference Build(std::span<const float> depth, std::uint32_t width, std::uint32_t height, DepthConvention convention);
		// Wraps mips produced elsewhere (for example read back from the GPU), checking their sizes.
		static HzbReference FromMips(
			std::uint32_t width, std::uint32_t height, DepthConvention convention, std::vector<std::vector<float>> mips);
		// Reduces one mip from the level below it (used to check GPU mips one at a time).
		static std::vector<float> Reduce(
			std::span<const float> source, HzbExtent sourceExtent, HzbExtent destination, DepthConvention convention);

		std::uint32_t GetWidth() const { return width; }

		std::uint32_t GetHeight() const { return height; }

		std::uint32_t GetMipCount() const { return static_cast<std::uint32_t>(mips.size()); }

		const HzbExtent& GetMipExtent(std::uint32_t mip) const { return extents.at(mip); }

		std::span<const float> GetMip(std::uint32_t mip) const { return mips.at(mip); }

		float Fetch(std::uint32_t mip, std::uint32_t x, std::uint32_t y) const;

		DepthConvention GetConvention() const { return convention; }

	  private:
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		DepthConvention convention = CanonicalDepthConvention;
		std::vector<HzbExtent> extents;
		std::vector<std::vector<float>> mips;
	};
} // namespace Swim::Render
