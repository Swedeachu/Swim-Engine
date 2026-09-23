#pragma once
#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

#include <algorithm>
#include <cstdint>

namespace Swim::Render
{
	// The modern renderer's canonical depth convention (the item 50 gate): clip depth
	// is in [0, 1] and reversed, so the near plane maps to 1 and the far plane (or
	// infinity) to 0. Depth buffers use D32Float, clear to 0 and pass GreaterEqual.
	// Forward is the conventional mapping (near 0, far 1), kept for views that need it;
	// the transitional renderer is unaffected. Culling is convention-agnostic; only the
	// HZB reduction and the occlusion test need to know which value is "farther".
	enum class DepthConvention : std::uint8_t
	{
		ReverseZ,
		Forward,
	};

	inline constexpr DepthConvention CanonicalDepthConvention = DepthConvention::ReverseZ;
	inline constexpr Rhi::Format CanonicalDepthFormat = Rhi::Format::D32Float;

	constexpr float DepthClearValue(DepthConvention convention)
	{
		return convention == DepthConvention::ReverseZ ? 0.0f : 1.0f;
	}

	constexpr Rhi::CompareOp DepthCompareOp(DepthConvention convention)
	{
		return convention == DepthConvention::ReverseZ ? Rhi::CompareOp::GreaterEqual : Rhi::CompareOp::LessEqual;
	}

	// Of two depths, the one farther from the camera (what an HZB texel keeps).
	constexpr float FartherDepth(DepthConvention convention, float a, float b)
	{
		return convention == DepthConvention::ReverseZ ? std::min(a, b) : std::max(a, b);
	}

	// Of two depths, the one nearer to the camera.
	constexpr float NearerDepth(DepthConvention convention, float a, float b)
	{
		return convention == DepthConvention::ReverseZ ? std::max(a, b) : std::min(a, b);
	}

	// True when `depth` lies strictly nearer to the camera than `reference`.
	constexpr bool IsNearer(DepthConvention convention, float depth, float reference)
	{
		return convention == DepthConvention::ReverseZ ? depth > reference : depth < reference;
	}
} // namespace Swim::Render
